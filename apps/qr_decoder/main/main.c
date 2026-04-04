/**
 * QR Decoder — continuous QR code scanner using the camera pipeline.
 *
 * Displays live camera feed with decoded QR text overlaid on screen.
 * Text appears on decode and fades out after 2 seconds. New decodes
 * replace the previous text and reset the timer.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board.h"
#include "board_config.h"
#include "board_pipeline.h"
#include "board_i2c.h"
#include "board_backlight.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "esp_cam_pipeline.h"
#include "cam_pipeline_qr.h"

static const char *TAG = "qr_decoder";

#if !BOARD_HAS_CAMERA
#error "This app requires a board with camera support (BOARD_HAS_CAMERA=1)"
#endif

/* ── FPS stats display ── */

#ifdef CONFIG_CAM_PIPELINE_DEBUG
static lv_obj_t *fps_label = NULL;
static cam_pipeline_handle_t s_pipeline = NULL;

/* Exponential moving average smoothing */
#define EMA_ALPHA_SLOW  0.3f   /* ~3 sec settle for stable rates */
#define EMA_ALPHA_FAST  0.7f   /* ~1 sec settle for detection rate */
static float ema_cam = 0, ema_disp = 0, ema_scan = 0, ema_det = 0;
static bool ema_initialized = false;

/* Detection counter — incremented from on_qr_decoded callback */
static volatile uint32_t s_detect_count = 0;
static uint32_t s_detect_count_prev = 0;
static int64_t s_detect_time_prev = 0;

static void fps_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!fps_label || !s_pipeline) return;

    cam_pipeline_debug_stats_t stats;
    if (cam_pipeline_get_debug_stats(s_pipeline, &stats) != ESP_OK) return;

    /* Compute detection rate from our own counter */
    int64_t now = esp_timer_get_time();
    float det_fps = 0;
    if (s_detect_time_prev > 0) {
        float elapsed = (now - s_detect_time_prev) / 1000000.0f;
        if (elapsed > 0) {
            uint32_t count = s_detect_count;
            det_fps = (count - s_detect_count_prev) / elapsed;
            s_detect_count_prev = count;
        }
    }
    s_detect_time_prev = now;

    if (!ema_initialized) {
        ema_cam = stats.camera_fps;
        ema_disp = stats.display_fps;
        ema_scan = stats.consumer_fps;
        ema_det = det_fps;
        ema_initialized = true;
    } else {
        ema_cam  = EMA_ALPHA_SLOW * stats.camera_fps   + (1 - EMA_ALPHA_SLOW) * ema_cam;
        ema_disp = EMA_ALPHA_SLOW * stats.display_fps   + (1 - EMA_ALPHA_SLOW) * ema_disp;
        ema_scan = EMA_ALPHA_SLOW * stats.consumer_fps  + (1 - EMA_ALPHA_SLOW) * ema_scan;
        ema_det  = EMA_ALPHA_FAST * det_fps             + (1 - EMA_ALPHA_FAST) * ema_det;
    }

    char buf[80];
    snprintf(buf, sizeof(buf), "cam: %.0f  disp: %.0f\nscan: %.0f  det: %.0f",
             ema_cam, ema_disp, ema_scan, ema_det);
    lv_label_set_text(fps_label, buf);
}
#endif

/* ── QR result display ── */

static lv_obj_t *qr_label = NULL;
static lv_timer_t *fade_timer = NULL;

#define QR_DISPLAY_TIMEOUT_MS  2000

/**
 * LVGL timer callback: hide QR text after timeout.
 * Runs in LVGL task context (lock already held by esp_lvgl_port).
 */
static void fade_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (qr_label) {
        lv_obj_add_flag(qr_label, LV_OBJ_FLAG_HIDDEN);
    }
    lv_timer_delete(fade_timer);
    fade_timer = NULL;
}

/**
 * QR decode callback — called from QR decode task (Core 1).
 * Updates the overlay label with decoded text.
 */
static void on_qr_decoded(const uint8_t *payload, size_t len,
                          const k_quirc_data_t *metadata, void *user_ctx)
{
    (void)user_ctx;

#ifdef CONFIG_CAM_PIPELINE_DEBUG
    __atomic_add_fetch(&s_detect_count, 1, __ATOMIC_RELAXED);
#endif

    char text[512];

    if (metadata->data_type == K_QUIRC_DATA_TYPE_BYTE) {
        /* Binary payload — display as hex */
        size_t max_bytes = (sizeof(text) - 1) / 3; /* "XX " per byte */
        size_t n = (len < max_bytes) ? len : max_bytes;
        for (size_t i = 0; i < n; i++) {
            snprintf(text + i * 3, 4, "%02X ", payload[i]);
        }
        if (n > 0) text[n * 3 - 1] = '\0'; /* trim trailing space */
        ESP_LOGI(TAG, "QR decoded (%zu bytes, binary): %s", len, text);
    } else {
        /* Text payload — display as-is */
        size_t copy_len = (len < sizeof(text) - 1) ? len : sizeof(text) - 1;
        memcpy(text, payload, copy_len);
        text[copy_len] = '\0';
        ESP_LOGI(TAG, "QR decoded (%zu bytes): %s", len, text);
    }

    if (lvgl_port_lock(50)) {
        if (qr_label) {
            lv_label_set_text(qr_label, text);
            lv_obj_clear_flag(qr_label, LV_OBJ_FLAG_HIDDEN);

            /* Reset or create the auto-hide timer */
            if (fade_timer) {
                lv_timer_reset(fade_timer);
            } else {
                fade_timer = lv_timer_create(fade_timer_cb,
                                             QR_DISPLAY_TIMEOUT_MS, NULL);
                lv_timer_set_repeat_count(fade_timer, 1);
            }
        }
        lvgl_port_unlock();
    }
}

/* ── Main ── */

void app_main(void)
{
    ESP_LOGI(TAG, "QR decoder starting");

    /* Initialize board hardware */
    lv_display_t *disp;
    lv_indev_t *touch;
    board_app_config_t app_cfg = { .landscape = false };
    board_init(&app_cfg, &disp, &touch);

    /* Build pipeline config from board defines */
    lv_obj_t *screen = NULL;
    if (lvgl_port_lock(0)) {
        screen = lv_screen_active();
        board_set_render_interval_ms(10);
        lvgl_port_unlock();
    }

    cam_pipeline_config_t pipeline_cfg = board_pipeline_default_config(
        screen, board_i2c_get_handle());

    /* Create the pipeline — starts camera streaming + display */
    cam_pipeline_handle_t pipeline = cam_pipeline_create(&pipeline_cfg);
    if (!pipeline) {
        ESP_LOGE(TAG, "Pipeline creation failed");
        board_backlight_set(100);
        board_run();
        return;
    }

    /* Semi-transparent overlay bars showing the QR scan region.
     * The QR consumer crops to a centered square (shorter dimension).
     * On a portrait display (w < h), bars go top and bottom. */
    uint32_t crop_size = (BOARD_LCD_H_RES < BOARD_LCD_V_RES)
                             ? BOARD_LCD_H_RES : BOARD_LCD_V_RES;
    uint32_t bar_h = (BOARD_LCD_V_RES - crop_size) / 2;
    uint32_t bar_w = (BOARD_LCD_H_RES - crop_size) / 2;

    if (lvgl_port_lock(0)) {
        /* Top/bottom bars (portrait) or left/right bars (landscape) */
        if (bar_h > 0) {
            /* Portrait: crop height, bars on top and bottom */
            lv_obj_t *bar_top = lv_obj_create(screen);
            lv_obj_remove_style_all(bar_top);
            lv_obj_set_size(bar_top, BOARD_LCD_H_RES, bar_h);
            lv_obj_set_style_bg_color(bar_top, lv_color_hex(0x000000), 0);
            lv_obj_set_style_bg_opa(bar_top, LV_OPA_50, 0);
            lv_obj_align(bar_top, LV_ALIGN_TOP_MID, 0, 0);

            lv_obj_t *bar_bot = lv_obj_create(screen);
            lv_obj_remove_style_all(bar_bot);
            lv_obj_set_size(bar_bot, BOARD_LCD_H_RES, bar_h);
            lv_obj_set_style_bg_color(bar_bot, lv_color_hex(0x000000), 0);
            lv_obj_set_style_bg_opa(bar_bot, LV_OPA_50, 0);
            lv_obj_align(bar_bot, LV_ALIGN_BOTTOM_MID, 0, 0);
        }
        if (bar_w > 0) {
            /* Landscape: crop width, bars on left and right */
            lv_obj_t *bar_left = lv_obj_create(screen);
            lv_obj_remove_style_all(bar_left);
            lv_obj_set_size(bar_left, bar_w, BOARD_LCD_V_RES);
            lv_obj_set_style_bg_color(bar_left, lv_color_hex(0x000000), 0);
            lv_obj_set_style_bg_opa(bar_left, LV_OPA_50, 0);
            lv_obj_align(bar_left, LV_ALIGN_LEFT_MID, 0, 0);

            lv_obj_t *bar_right = lv_obj_create(screen);
            lv_obj_remove_style_all(bar_right);
            lv_obj_set_size(bar_right, bar_w, BOARD_LCD_V_RES);
            lv_obj_set_style_bg_color(bar_right, lv_color_hex(0x000000), 0);
            lv_obj_set_style_bg_opa(bar_right, LV_OPA_50, 0);
            lv_obj_align(bar_right, LV_ALIGN_RIGHT_MID, 0, 0);
        }

        /* QR result label — on top of everything */
        qr_label = lv_label_create(screen);
        lv_obj_set_width(qr_label, BOARD_LCD_H_RES - 20);
        lv_label_set_long_mode(qr_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(qr_label, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_text_font(qr_label, &lv_font_montserrat_24, 0);
        lv_obj_set_style_bg_color(qr_label, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(qr_label, LV_OPA_70, 0);
        lv_obj_set_style_pad_all(qr_label, 8, 0);
        lv_obj_align(qr_label, LV_ALIGN_BOTTOM_MID, 0, -20);
        lv_label_set_text(qr_label, "");
        lv_obj_add_flag(qr_label, LV_OBJ_FLAG_HIDDEN);

#ifdef CONFIG_CAM_PIPELINE_DEBUG
        /* FPS stats label — centered in the top overlay bar */
        fps_label = lv_label_create(screen);
        lv_obj_set_width(fps_label, BOARD_LCD_H_RES);
        lv_obj_set_style_text_color(fps_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(fps_label, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_align(fps_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_ver(fps_label, 8, 0);
        lv_obj_align(fps_label, LV_ALIGN_TOP_MID, 0, 0);
        lv_label_set_text(fps_label, "---");
#endif

        lvgl_port_unlock();
    }

#ifdef CONFIG_CAM_PIPELINE_DEBUG
    /* Start FPS stats polling timer */
    s_pipeline = pipeline;
    if (lvgl_port_lock(0)) {
        lv_timer_create(fps_timer_cb, 1000, NULL);
        lvgl_port_unlock();
    }
#endif

    /* Start QR decode consumer */
    cam_pipeline_qr_config_t qr_cfg = {
        .pipeline     = pipeline,
        .frame_width  = BOARD_LCD_H_RES,
        .frame_height = BOARD_LCD_V_RES,
        .on_decoded   = on_qr_decoded,
        .user_ctx     = NULL,
    };
    cam_pipeline_qr_handle_t qr = cam_pipeline_qr_create(&qr_cfg);
    if (!qr) {
        ESP_LOGE(TAG, "QR consumer creation failed");
    }

    board_backlight_set(100);

    ESP_LOGI(TAG, "QR decoder running (%dx%d)", BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    board_run();
}
