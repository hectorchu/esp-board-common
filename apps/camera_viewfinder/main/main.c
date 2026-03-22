#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "board.h"
#include "board_config.h"
#include "board_camera.h"
#include "board_backlight.h"
#include "esp_camera.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "viewfinder";

#if !BOARD_HAS_CAMERA
#error "This app requires a board with camera support (BOARD_HAS_CAMERA=1)"
#endif

/* Select largest square camera frame that fits the display's short axis */
#if BOARD_LCD_H_RES < BOARD_LCD_V_RES
#define DISPLAY_MIN_DIM BOARD_LCD_H_RES
#else
#define DISPLAY_MIN_DIM BOARD_LCD_V_RES
#endif

#if DISPLAY_MIN_DIM >= 320
#define CAM_FRAMESIZE   FRAMESIZE_320X320
#define CAM_DIM         320
#elif DISPLAY_MIN_DIM >= 240
#define CAM_FRAMESIZE   FRAMESIZE_240X240
#define CAM_DIM         240
#elif DISPLAY_MIN_DIM >= 128
#define CAM_FRAMESIZE   FRAMESIZE_128X128
#define CAM_DIM         128
#else
#define CAM_FRAMESIZE   FRAMESIZE_96X96
#define CAM_DIM         96
#endif

#define CAM_BPP         2   /* RGB565 */
#define CAM_FRAME_SIZE  (CAM_DIM * CAM_DIM * CAM_BPP)

/* Persistent camera frame buffer and LVGL image descriptor */
static uint8_t *cam_buf;
static lv_image_dsc_t cam_img_dsc;

static void camera_task(void *param)
{
    lv_obj_t *img = (lv_obj_t *)param;

    ESP_LOGI(TAG, "Camera task started (%dx%d RGB565)", CAM_DIM, CAM_DIM);

    while (1) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Frame grab failed");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* Burst-copy then byte-swap in place (both potentially SIMD-optimized) */
        memcpy(cam_buf, fb->buf, fb->len);
        esp_camera_fb_return(fb);
        lv_draw_sw_rgb565_swap(cam_buf, CAM_DIM * CAM_DIM);

        /* Update LVGL image widget */
        if (lvgl_port_lock(100)) {
            lv_image_set_src(img, &cam_img_dsc);
            lv_obj_invalidate(img);
            lvgl_port_unlock();
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

    /* Portrait mode: square camera image centered on panel */
    board_app_config_t app_cfg = { .landscape = false };
    board_init(&app_cfg, &disp, &touch);

    /* Initialize camera with square frame matching display */
    board_camera_init(BOARD_I2C_PORT, CAM_FRAMESIZE);
    ESP_LOGI(TAG, "Camera initialized (%dx%d)", CAM_DIM, CAM_DIM);

    /* Allocate persistent PSRAM buffer for camera frames */
    cam_buf = heap_caps_malloc(CAM_FRAME_SIZE, MALLOC_CAP_SPIRAM);
    assert(cam_buf != NULL);

    /* Initialize static image descriptor */
    memset(&cam_img_dsc, 0, sizeof(cam_img_dsc));
    cam_img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    cam_img_dsc.header.w = CAM_DIM;
    cam_img_dsc.header.h = CAM_DIM;
    cam_img_dsc.data_size = CAM_FRAME_SIZE;
    cam_img_dsc.data = cam_buf;

    /* Create LVGL image widget for camera frames */
    lv_obj_t *img = NULL;
    if (lvgl_port_lock(0)) {
        img = lv_image_create(lv_screen_active());
        lv_obj_set_size(img, CAM_DIM, CAM_DIM);
        lv_obj_center(img);
        lvgl_port_unlock();
    }
    assert(img != NULL);

    /* Enable fast render interval for real-time viewfinder */
    if (lvgl_port_lock(0)) {
        board_set_render_interval_ms(10);
        lvgl_port_unlock();
    }

    /* Turn on backlight after first frame setup */
    board_backlight_set(100);

    /* Start camera capture task on core 0 (LVGL runs on core 1) */
    xTaskCreatePinnedToCore(camera_task, "camera", 4096, img,
                            5, NULL, 0);

    ESP_LOGI(TAG, "Viewfinder running");
    board_run();
}
