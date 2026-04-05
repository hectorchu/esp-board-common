/**
 * Camera viewfinder — live camera feed displayed on the board's LCD.
 *
 * Uses the unified board_camera API, which dispatches to DVP or CSI
 * depending on BOARD_CAMERA_INTERFACE in board_config.h.
 *
 * On ESP32-P4 boards, uses the PPA (Pixel Processing Accelerator) hardware
 * engine for zero-CPU-cost scaling from the sensor resolution to the display.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "board.h"
#include "board_config.h"
#include "board_camera.h"
#include "board_i2c.h"
#include "board_backlight.h"
#include "esp_lv_adapter.h"
#include "lvgl.h"

#if SOC_PPA_SUPPORTED
#include "driver/ppa.h"
#endif

static const char *TAG = "viewfinder";

#if !BOARD_HAS_CAMERA
#error "This app requires a board with camera support (BOARD_HAS_CAMERA=1)"
#endif

/* Display dimensions for the viewfinder crop */
#define VIEW_W      BOARD_LCD_H_RES
#define VIEW_H      BOARD_LCD_V_RES
#define VIEW_BPP    2   /* RGB565 */
#define VIEW_SIZE   (VIEW_W * VIEW_H * VIEW_BPP)

/* Persistent camera frame buffer and LVGL image descriptor */
static uint8_t *cam_buf;
static lv_image_dsc_t cam_img_dsc;

#if SOC_PPA_SUPPORTED
static ppa_client_handle_t ppa_srm_handle;
#endif

static void camera_task(void *param)
{
    lv_obj_t *img = (lv_obj_t *)param;

    ESP_LOGI(TAG, "Camera task started (%dx%d RGB565)", VIEW_W, VIEW_H);

    while (1) {
        board_camera_frame_t frame;
        esp_err_t err = board_camera_fb_get(&frame, 5000);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Frame grab failed");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        uint16_t src_w = frame.width;
        uint16_t src_h = frame.height;

#if SOC_PPA_SUPPORTED
        /* Hardware-accelerated scale via PPA SRM engine.
         * Use uniform scaling to maintain aspect ratio and fill the screen.
         * Pick the larger scale factor so the image covers the display
         * completely, then center-crop the input to match exactly. */
        float sx = (float)VIEW_W / src_w;
        float sy = (float)VIEW_H / src_h;
        float scale = (sx > sy) ? sx : sy;  /* fill (not fit) */
        uint32_t in_w = (uint32_t)(VIEW_W / scale);
        uint32_t in_h = (uint32_t)(VIEW_H / scale);
        uint32_t off_x = (src_w - in_w) / 2;
        uint32_t off_y = (src_h - in_h) / 2;

        ppa_srm_oper_config_t srm_cfg = {
            .in = {
                .buffer = frame.buf,
                .pic_w = src_w,
                .pic_h = src_h,
                .block_w = in_w,
                .block_h = in_h,
                .block_offset_x = off_x,
                .block_offset_y = off_y,
                .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            },
            .out = {
                .buffer = cam_buf,
                .buffer_size = VIEW_SIZE,
                .pic_w = VIEW_W,
                .pic_h = VIEW_H,
                .block_offset_x = 0,
                .block_offset_y = 0,
                .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            },
            .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
            .scale_x = scale,
            .scale_y = scale,
            .mirror_x = false,
            .mirror_y = false,
            .mode = PPA_TRANS_MODE_BLOCKING,
        };
        ppa_do_scale_rotate_mirror(ppa_srm_handle, &srm_cfg);
#else
        /* Software fallback: nearest-neighbor downscale or center-copy */
        uint16_t *src_px = (uint16_t *)frame.buf;
        uint16_t *dst_px = (uint16_t *)cam_buf;

        if (src_w > VIEW_W || src_h > VIEW_H) {
            for (int dy = 0; dy < VIEW_H; dy++) {
                int sy = dy * src_h / VIEW_H;
                for (int dx = 0; dx < VIEW_W; dx++) {
                    int sx = dx * src_w / VIEW_W;
                    dst_px[dy * VIEW_W + dx] = src_px[sy * src_w + sx];
                }
            }
        } else {
            uint16_t off_x = (VIEW_W - src_w) / 2;
            uint16_t off_y = (VIEW_H - src_h) / 2;
            for (int y = 0; y < src_h; y++) {
                memcpy(&dst_px[(off_y + y) * VIEW_W + off_x],
                       &src_px[y * src_w],
                       src_w * VIEW_BPP);
            }
        }
#endif
        board_camera_fb_return(&frame);

#if BOARD_CAMERA_INTERFACE == CAMERA_DVP
        /* DVP sensors output big-endian RGB565; swap to little-endian for LVGL */
        lv_draw_sw_rgb565_swap(cam_buf, VIEW_W * VIEW_H);
#endif

        /* Update LVGL image widget */
        if (esp_lv_adapter_lock(100) == ESP_OK) {
            lv_image_set_src(img, &cam_img_dsc);
            lv_obj_invalidate(img);
            esp_lv_adapter_unlock();
        }

        /* Minimal yield — camera capture rate is the natural limiter */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Camera viewfinder starting");

    lv_display_t *disp;
    lv_indev_t *touch;

    board_app_config_t app_cfg = { .landscape = false };
    board_init(&app_cfg, &disp, &touch);

    /* Initialize camera — pass desired dimensions as hints */
    esp_err_t cam_err = board_camera_init(board_i2c_get_handle(), VIEW_W, VIEW_H);
    if (cam_err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(cam_err));
        board_backlight_set(100);
        board_run();
        return;
    }
    ESP_LOGI(TAG, "Camera initialized (view: %dx%d)", VIEW_W, VIEW_H);

#if SOC_PPA_SUPPORTED
    /* Register PPA SRM client for hardware-accelerated frame scaling */
    ppa_client_config_t ppa_cfg = {
        .oper_type = PPA_OPERATION_SRM,
    };
    ESP_ERROR_CHECK(ppa_register_client(&ppa_cfg, &ppa_srm_handle));
    ESP_LOGI(TAG, "PPA SRM engine registered for hardware scaling");
#endif

    /* Allocate persistent PSRAM buffer for display-sized frames (cache-aligned for PPA DMA) */
    cam_buf = heap_caps_aligned_calloc(128, 1, VIEW_SIZE, MALLOC_CAP_SPIRAM);
    assert(cam_buf != NULL);

    /* Initialize static image descriptor */
    memset(&cam_img_dsc, 0, sizeof(cam_img_dsc));
    cam_img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    cam_img_dsc.header.w = VIEW_W;
    cam_img_dsc.header.h = VIEW_H;
    cam_img_dsc.data_size = VIEW_SIZE;
    cam_img_dsc.data = cam_buf;

    /* Create LVGL image widget filling the screen */
    lv_obj_t *img = NULL;
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        img = lv_image_create(lv_screen_active());
        lv_obj_set_size(img, VIEW_W, VIEW_H);
        lv_obj_center(img);
        esp_lv_adapter_unlock();
    }
    assert(img != NULL);

    /* Enable fast render interval for real-time viewfinder */
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        board_set_render_interval_ms(10);
        esp_lv_adapter_unlock();
    }

    board_backlight_set(100);

    /* Start camera capture task (large stack for V4L2 ioctls + ISP on P4) */
    xTaskCreatePinnedToCore(camera_task, "camera", 16384, img,
                            5, NULL, 0);

    ESP_LOGI(TAG, "Viewfinder running (%dx%d fullscreen)", VIEW_W, VIEW_H);
    board_run();
}
