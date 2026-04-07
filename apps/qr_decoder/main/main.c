/**
 * QR Decoder — continuous QR code scanner using the camera pipeline.
 *
 * Displays live camera feed with decoded QR text overlaid on screen.
 * Text appears on decode and fades out after 2 seconds. New decodes
 * replace the previous text and reset the timer.
 */
#include <inttypes.h>
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
#include "board_log_flash.h"

/* SPI dummy-draw: direct text overlay (LVGL is stopped) */
#if BOARD_DISPLAY_DRIVER != DISPLAY_ST7701
#include "overlay_text.h"
#include "board_pipeline_display_lvgl.h"
#include "freertos/timers.h"
static overlay_text_t *s_overlay = NULL;
#endif

static const char *TAG = "qr_decoder";

#if !BOARD_HAS_CAMERA
#error "This app requires a board with camera support (BOARD_HAS_CAMERA=1)"
#endif

/* ── FPS stats display ── */

#ifdef CONFIG_CAM_PIPELINE_DEBUG
#if BOARD_DISPLAY_DRIVER == DISPLAY_ST7701 && !BOARD_LANDSCAPE
static lv_obj_t *fps_label = NULL;
#endif
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

#endif /* CONFIG_CAM_PIPELINE_DEBUG */

/* ── Pre-rendered rotated overlay for DSI landscape ──
 * transform_rotation on widgets is incompatible with direct_mode DSI.
 * Instead: render text into a hidden canvas, CPU-rotate 90° CCW,
 * display as a normal lv_image.  Supports both:
 *   - RGB565 (opaque, black bg) for gap-area overlays (no alpha blend cost)
 *   - ARGB8888 (transparent bg) for camera-area overlays
 * Bounding-box crop: only the tight text region is rotated + blended. */
#if BOARD_LANDSCAPE && BOARD_DISPLAY_DRIVER == DISPLAY_ST7701
#include "esp_heap_caps.h"

typedef struct {
    lv_obj_t *canvas_src;       /* hidden canvas for horizontal text rendering */
    lv_obj_t *img_rotated;      /* visible image showing rotated result */
    lv_image_dsc_t img_dsc;
    uint8_t *rotated_buf;
    int canvas_w, canvas_h;     /* source canvas dimensions */
    lv_color_format_t cf;
    uint8_t bpp;                /* 2 (RGB565) or 4 (ARGB8888) */
    bool opaque;                /* true: black bg, false: transparent bg */
    int base_x, base_y;        /* position of full (uncropped) rotated image */
} rotated_overlay_t;

/* ── Cropped rotation helpers ──
 * Rotate a sub-rect (cx,cy,cw,ch) of a source buffer with stride src_w.
 * Output is a compact ch×cw image (dimensions swapped by 90° CCW). */

static void rotate_90ccw_crop_argb8888(const uint32_t *src, int src_w,
                                       int cx, int cy, int cw, int ch,
                                       uint32_t *dst)
{
    for (int y = 0; y < ch; y++) {
        for (int x = 0; x < cw; x++) {
            dst[(cw - 1 - x) * ch + y] = src[(cy + y) * src_w + (cx + x)];
        }
    }
}

static void rotate_90ccw_crop_rgb565(const uint16_t *src, int src_w,
                                     int cx, int cy, int cw, int ch,
                                     uint16_t *dst)
{
    for (int y = 0; y < ch; y++) {
        for (int x = 0; x < cw; x++) {
            dst[(cw - 1 - x) * ch + y] = src[(cy + y) * src_w + (cx + x)];
        }
    }
}

/** Find tight bounding box of non-background pixels.
 *  ARGB8888: pixel with alpha > 0.  RGB565: pixel != 0 (non-black). */
static bool overlay_find_bbox(const uint8_t *buf, int w, int h, int bpp,
                              int *ox, int *oy, int *ow, int *oh)
{
    int min_x = w, min_y = h, max_x = -1, max_y = -1;

    if (bpp == 4) {
        const uint32_t *p = (const uint32_t *)buf;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                if ((p[y * w + x] >> 24) != 0) {
                    if (x < min_x) min_x = x;
                    if (x > max_x) max_x = x;
                    if (y < min_y) min_y = y;
                    if (y > max_y) max_y = y;
                }
            }
        }
    } else {
        const uint16_t *p = (const uint16_t *)buf;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                if (p[y * w + x] != 0) {
                    if (x < min_x) min_x = x;
                    if (x > max_x) max_x = x;
                    if (y < min_y) min_y = y;
                    if (y > max_y) max_y = y;
                }
            }
        }
    }

    if (max_x < 0) return false;  /* canvas is empty */
    *ox = min_x;  *oy = min_y;
    *ow = max_x - min_x + 1;
    *oh = max_y - min_y + 1;
    return true;
}

/** Create a rotated overlay.  Allocates canvas + rotated buffer from PSRAM.
 *  pos_x/pos_y = where the full (uncropped) rotated image would sit. */
static bool rotated_overlay_init(rotated_overlay_t *ov, lv_obj_t *parent,
                                 int canvas_w, int canvas_h,
                                 lv_color_format_t cf,
                                 int pos_x, int pos_y)
{
    ov->canvas_w = canvas_w;
    ov->canvas_h = canvas_h;
    ov->cf = cf;
    ov->bpp = (cf == LV_COLOR_FORMAT_ARGB8888) ? 4 : 2;
    ov->opaque = (cf == LV_COLOR_FORMAT_RGB565);
    ov->base_x = pos_x;
    ov->base_y = pos_y;

    size_t src_size = (size_t)canvas_w * canvas_h * ov->bpp;
    uint8_t *src_buf = heap_caps_malloc(src_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!src_buf) {
        ESP_LOGE("overlay", "Failed to alloc %dx%d src canvas (%zu bytes)",
                 canvas_w, canvas_h, src_size);
        return false;
    }
    memset(src_buf, 0, src_size);
    ov->canvas_src = lv_canvas_create(parent);
    lv_canvas_set_buffer(ov->canvas_src, src_buf, canvas_w, canvas_h, cf);
    lv_obj_add_flag(ov->canvas_src, LV_OBJ_FLAG_HIDDEN);

    /* Worst-case rotated buffer (full canvas, dimensions swapped) */
    size_t buf_size = (size_t)canvas_h * canvas_w * ov->bpp;
    ov->rotated_buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ov->rotated_buf) {
        ESP_LOGE("overlay", "Failed to alloc rotated buf (%zu bytes)", buf_size);
        return false;
    }
    memset(ov->rotated_buf, 0, buf_size);

    ov->img_dsc.header.cf = cf;
    ov->img_dsc.header.w = canvas_h;
    ov->img_dsc.header.h = canvas_w;
    ov->img_dsc.data_size = buf_size;
    ov->img_dsc.data = ov->rotated_buf;

    ov->img_rotated = lv_image_create(parent);
    lv_image_set_src(ov->img_rotated, &ov->img_dsc);
    lv_obj_set_pos(ov->img_rotated, pos_x, pos_y);
    return true;
}

/** Render text, bbox-crop, rotate 90° CCW, update display image. */
static void rotated_overlay_update(rotated_overlay_t *ov, const char *text,
                                   lv_color_t color)
{
    lv_canvas_fill_bg(ov->canvas_src, lv_color_black(),
                      ov->opaque ? LV_OPA_COVER : LV_OPA_TRANSP);

    lv_layer_t layer;
    lv_canvas_init_layer(ov->canvas_src, &layer);
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.color = color;
    dsc.font = &lv_font_montserrat_24;
    dsc.text = text;
    lv_area_t coords = {4, 4, ov->canvas_w - 4, ov->canvas_h - 4};
    lv_draw_label(&layer, &dsc, &coords);
    lv_canvas_finish_layer(ov->canvas_src, &layer);

    const uint8_t *src_buf = lv_canvas_get_buf(ov->canvas_src);

    /* Tight bounding box around rendered text */
    int cx, cy, cw, ch;
    if (!overlay_find_bbox(src_buf, ov->canvas_w, ov->canvas_h, ov->bpp,
                           &cx, &cy, &cw, &ch)) {
        lv_obj_add_flag(ov->img_rotated, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    /* Rotate only the cropped region */
    if (ov->bpp == 4) {
        rotate_90ccw_crop_argb8888((const uint32_t *)src_buf, ov->canvas_w,
                                   cx, cy, cw, ch,
                                   (uint32_t *)ov->rotated_buf);
    } else {
        rotate_90ccw_crop_rgb565((const uint16_t *)src_buf, ov->canvas_w,
                                 cx, cy, cw, ch,
                                 (uint16_t *)ov->rotated_buf);
    }

    /* Update descriptor to cropped rotated dimensions */
    ov->img_dsc.header.w = ch;   /* src crop height → rotated width */
    ov->img_dsc.header.h = cw;   /* src crop width  → rotated height */
    ov->img_dsc.data_size = (size_t)ch * cw * ov->bpp;

    /* Adjust position: source crop (cx,cy) → rotated offset (cy, canvas_w-cx-cw) */
    lv_obj_set_pos(ov->img_rotated,
                   ov->base_x + cy,
                   ov->base_y + (ov->canvas_w - cx - cw));

    lv_image_set_src(ov->img_rotated, &ov->img_dsc);
    lv_obj_clear_flag(ov->img_rotated, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(ov->img_rotated);
}

static void rotated_overlay_show(rotated_overlay_t *ov, bool visible)
{
    if (visible)
        lv_obj_clear_flag(ov->img_rotated, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(ov->img_rotated, LV_OBJ_FLAG_HIDDEN);
}

/* ── Canvas dimensions ──
 * Canvas is a horizontal text buffer; after 90° CCW rotation:
 *   canvas_w → rotated image height (portrait Y span = landscape X extent)
 *   canvas_h → rotated image width  (portrait X span = landscape Y extent) */

/* Camera square is centered: gap = (BOARD_LCD_V_RES - square) / 2 per side.
 * For 480x800 display with 480x480 camera → 160px gap on each side. */
#define LANDSCAPE_GAP  ((BOARD_LCD_V_RES - BOARD_LCD_H_RES) / 2)

/* FPS: 4 short lines in the landscape-right gap (portrait top gap).
 * RGB565 opaque — no alpha blend cost on the black gap area. */
#define FPS_CANVAS_W   130   /* landscape X extent (fits within 160px gap) */
#define FPS_CANVAS_H   130   /* landscape Y extent (4 lines at ~28px each) */

/* QR result: full landscape width, transparent over camera feed. */
#define QR_CANVAS_W    BOARD_LCD_V_RES   /* 800 — full landscape width */
#define QR_CANVAS_H    80

#ifdef CONFIG_CAM_PIPELINE_DEBUG
static rotated_overlay_t fps_overlay;
#endif
static rotated_overlay_t qr_overlay;
#endif /* BOARD_LANDSCAPE && DISPLAY_ST7701 */

#ifdef CONFIG_CAM_PIPELINE_DEBUG

/* Shared FPS stats update — called from platform-specific timer callback */
static void update_fps_stats(void)
{
    if (!s_pipeline) return;

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
#if BOARD_LANDSCAPE && BOARD_DISPLAY_DRIVER == DISPLAY_ST7701
    /* One stat per line — fits the narrow landscape gap strip */
    snprintf(buf, sizeof(buf), "cam: %.0f\ndisp: %.0f\nscan: %.0f\ndet: %.0f",
             ema_cam, ema_disp, ema_scan, ema_det);
    if (fps_overlay.img_rotated) {
        rotated_overlay_update(&fps_overlay, buf, lv_color_white());
    }
#elif BOARD_DISPLAY_DRIVER != DISPLAY_ST7701
#if BOARD_LANDSCAPE
    /* One stat per line — fits the narrow 80px landscape gap strip */
    snprintf(buf, sizeof(buf), "cam: %.0f\ndisp: %.0f\nscan: %.0f\ndet: %.0f",
             ema_cam, ema_disp, ema_scan, ema_det);
#else
    snprintf(buf, sizeof(buf), "cam: %.0f  disp: %.0f\nscan: %.0f  det: %.0f",
             ema_cam, ema_disp, ema_scan, ema_det);
#endif
    overlay_text_set_fps(s_overlay, buf);
#else
    snprintf(buf, sizeof(buf), "cam: %.0f  disp: %.0f\nscan: %.0f  det: %.0f",
             ema_cam, ema_disp, ema_scan, ema_det);
    if (fps_label) {
        lv_label_set_text(fps_label, buf);
    }
#endif
}

#if BOARD_DISPLAY_DRIVER != DISPLAY_ST7701
/* SPI dummy-draw: LVGL is stopped, use FreeRTOS software timer */
static void fps_freertos_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    update_fps_stats();
}
#else
/* DSI: LVGL is running, use LVGL timer */
static void fps_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_fps_stats();
}
#endif

#endif /* CONFIG_CAM_PIPELINE_DEBUG */

/* ── QR result display ── */

#if BOARD_DISPLAY_DRIVER == DISPLAY_ST7701 && !BOARD_LANDSCAPE
static lv_obj_t *qr_label = NULL;
#endif

#define QR_DISPLAY_TIMEOUT_MS  2000

/* Auto-hide timer + callback only for DSI paths (SPI overlay handles internally) */
#if BOARD_DISPLAY_DRIVER == DISPLAY_ST7701
static lv_timer_t *fade_timer = NULL;

/**
 * LVGL timer callback: hide QR text after timeout.
 * Runs in LVGL task context (lock already held by adapter).
 */
static void fade_timer_cb(lv_timer_t *timer)
{
    (void)timer;
#if BOARD_LANDSCAPE
    rotated_overlay_show(&qr_overlay, false);
#else
    if (qr_label) {
        lv_obj_add_flag(qr_label, LV_OBJ_FLAG_HIDDEN);
    }
#endif
    lv_timer_delete(fade_timer);
    fade_timer = NULL;
}
#endif /* DISPLAY_ST7701 */

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

#if BOARD_DISPLAY_DRIVER != DISPLAY_ST7701
    /* SPI dummy-draw: direct overlay (no LVGL lock needed) */
    overlay_text_set_qr(s_overlay, text, QR_DISPLAY_TIMEOUT_MS);
#else
    if (lvgl_port_lock(50)) {
#if BOARD_LANDSCAPE
        if (qr_overlay.img_rotated) {
            rotated_overlay_update(&qr_overlay, text, lv_color_hex(0x00FF00));
            rotated_overlay_show(&qr_overlay, true);
        }
#else
        if (qr_label) {
            lv_label_set_text(qr_label, text);
            lv_obj_clear_flag(qr_label, LV_OBJ_FLAG_HIDDEN);
        }
#endif

        /* Reset or create the auto-hide timer */
        if (fade_timer) {
            lv_timer_reset(fade_timer);
        } else {
            fade_timer = lv_timer_create(fade_timer_cb,
                                         QR_DISPLAY_TIMEOUT_MS, NULL);
            lv_timer_set_repeat_count(fade_timer, 1);
        }
        lvgl_port_unlock();
    }
#endif /* DISPLAY_ST7701 */
}

/* ── Delayed log dump task ── */

void log_dump_task(void *param)
{
    (void)param;
    vTaskDelay(pdMS_TO_TICKS(8000));
    board_log_flash_dump();
    vTaskDelete(NULL);
}

/* ── Main ── */

void app_main(void)
{
    /* Flash log — must be first so it captures all subsequent output */
    board_log_flash_init();

    ESP_LOGI(TAG, "QR decoder starting");

    /* Initialize board hardware.
     * Landscape: LVGL and panel stay in portrait orientation.
     * DSI: rotated canvas overlays handle visual rotation.
     * SPI dummy-draw: panel stays portrait — camera rotates with the board,
     *   so MADCTL swap_xy would just rotate the image an extra 90°.
     *   Text overlays rendered rotated manually. */
    lv_display_t *disp;
    lv_indev_t *touch;
#if BOARD_LANDSCAPE
    ESP_LOGI(TAG, "Landscape mode: panel stays portrait, overlays handle rotation");
    board_app_config_t app_cfg = { .landscape = false };
#else
    board_app_config_t app_cfg = { .landscape = BOARD_LANDSCAPE };
#endif
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

    /* Square crop: use shorter logical dimension for both axes.
     * Camera fill+crop produces a square frame — no wasted pixels for
     * the QR consumer, and the LVGL display driver centers it. */
    uint32_t square = (BOARD_DISP_H_RES < BOARD_DISP_V_RES)
                          ? BOARD_DISP_H_RES : BOARD_DISP_V_RES;
    pipeline_cfg.display_width = square;
    pipeline_cfg.display_height = square;

#if BOARD_DISPLAY_DRIVER != DISPLAY_ST7701
    /* SPI dummy-draw: create direct text overlay (portrait gap areas or
     * landscape rotated overlays — see overlay_text.c for both paths). */
    {
        overlay_text_config_t ov_cfg = {
            .panel_width    = BOARD_LCD_H_RES,
            .panel_height   = BOARD_LCD_V_RES,
            .camera_height  = square,
            .byte_swap      = true,   /* SPI panels need byte-swap */
            .landscape      = BOARD_LANDSCAPE,
            .panel_handle   = board_get_panel_handle(),
#if BOARD_LANDSCAPE
            .font           = &lv_font_montserrat_14,
#else
            .font           = &lv_font_montserrat_24,
#endif
        };
        s_overlay = overlay_text_create(&ov_cfg);
        if (s_overlay) {
            /* Register callback for QR auto-hide timing (does not modify frame) */
            board_pipeline_lvgl_display_config_t *disp_cfg =
                (board_pipeline_lvgl_display_config_t *)pipeline_cfg.display_config;
            disp_cfg->overlay_cb = overlay_text_cb;
            disp_cfg->overlay_cb_ctx = s_overlay;
            ESP_LOGI(TAG, "Gap-area text overlay enabled for SPI dummy-draw");
        }
    }
#endif

    /* Create the pipeline — starts camera streaming + display */
    cam_pipeline_handle_t pipeline = cam_pipeline_create(&pipeline_cfg);
    if (!pipeline) {
        ESP_LOGE(TAG, "Pipeline creation failed");
        board_backlight_set(100);
        board_run();
        return;
    }

    /* Black background behind the centered square */
    if (lvgl_port_lock(0)) {
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);

#if BOARD_LANDSCAPE && BOARD_DISPLAY_DRIVER == DISPLAY_ST7701
        /* ── DSI landscape: pre-rendered rotated overlays ──
         * Canvas text rendering + CPU 90° CCW rotation + bbox crop.
         * Avoids transform_rotation + direct_mode incompatibility.
         * RGB565 for opaque gap overlays, ARGB8888 for transparent camera overlays.
         *
         * Portrait coords → landscape viewing:
         *   portrait top   (y=0)   → landscape right
         *   portrait bottom(y=800) → landscape left  */

        /* QR result overlay — landscape bottom (portrait right edge), transparent */
        if (rotated_overlay_init(&qr_overlay, screen,
                                 QR_CANVAS_W, QR_CANVAS_H,
                                 LV_COLOR_FORMAT_ARGB8888,
                                 BOARD_LCD_H_RES - QR_CANVAS_H - 4,  /* near right = landscape bottom */
                                 0)) {                                /* top of portrait y = landscape right edge */
            rotated_overlay_show(&qr_overlay, false);
            ESP_LOGI(TAG, "QR overlay: %dx%d ARGB8888 (landscape bottom)",
                     QR_CANVAS_W, QR_CANVAS_H);
        }

#ifdef CONFIG_CAM_PIPELINE_DEBUG
        /* FPS stats overlay — landscape-right gap (portrait top gap).
         * RGB565 opaque: no alpha blend cost on the black gap area.
         * base_y positions the full canvas so text starts near the camera
         * boundary (left edge of the gap in landscape) and extends toward
         * the screen edge (rightward in landscape = toward portrait y=0). */
        if (rotated_overlay_init(&fps_overlay, screen,
                                 FPS_CANVAS_W, FPS_CANVAS_H,
                                 LV_COLOR_FORMAT_RGB565,
                                 8,                                   /* portrait left pad = landscape top pad */
                                 LANDSCAPE_GAP - FPS_CANVAS_W)) {    /* align canvas bottom near camera boundary */
            ESP_LOGI(TAG, "FPS overlay: %dx%d RGB565 (landscape-right gap)",
                     FPS_CANVAS_W, FPS_CANVAS_H);
        }
#endif

#elif BOARD_DISPLAY_DRIVER != DISPLAY_ST7701
        /* ── SPI portrait dummy-draw: text in panel gap areas via overlay_text ──
         * LVGL is stopped (dummy-draw mode) so no widgets or timers work.
         * See docs/knowledge/text-overlay-architecture.md for why each path
         * uses a different approach. */

#else
        /* ── DSI portrait: standard LVGL label widgets ── */

        qr_label = lv_label_create(screen);
        lv_obj_set_width(qr_label, BOARD_DISP_H_RES - 20);
        lv_label_set_long_mode(qr_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(qr_label, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_text_font(qr_label, &lv_font_montserrat_24, 0);
        lv_obj_set_style_pad_all(qr_label, 8, 0);
        lv_obj_align(qr_label, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_label_set_text(qr_label, "");
        lv_obj_add_flag(qr_label, LV_OBJ_FLAG_HIDDEN);

#ifdef CONFIG_CAM_PIPELINE_DEBUG
        fps_label = lv_label_create(screen);
        lv_obj_set_width(fps_label, BOARD_DISP_H_RES);
        lv_obj_set_style_text_color(fps_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(fps_label, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_align(fps_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_ver(fps_label, 8, 0);
        lv_obj_align(fps_label, LV_ALIGN_TOP_MID, 0, 0);
        lv_label_set_text(fps_label, "---");
#endif

#endif /* overlay path selection */

        lvgl_port_unlock();
    }

#ifdef CONFIG_CAM_PIPELINE_DEBUG
    /* Start FPS stats polling timer */
    s_pipeline = pipeline;
#if BOARD_DISPLAY_DRIVER != DISPLAY_ST7701
    /* SPI: LVGL is stopped — use FreeRTOS software timer */
    {
        TimerHandle_t fps_tmr = xTimerCreate("fps", pdMS_TO_TICKS(1000),
                                              pdTRUE, NULL, fps_freertos_timer_cb);
        if (fps_tmr) xTimerStart(fps_tmr, 0);
    }
#else
    /* DSI: LVGL is running — use LVGL timer */
    if (lvgl_port_lock(0)) {
        lv_timer_create(fps_timer_cb, 1000, NULL);
        lvgl_port_unlock();
    }
#endif
#endif

    /* Start QR decode consumer */
    cam_pipeline_qr_config_t qr_cfg = {
        .pipeline     = pipeline,
        .frame_width  = square,
        .frame_height = square,
        .on_decoded   = on_qr_decoded,
        .user_ctx     = NULL,
    };
    cam_pipeline_qr_handle_t qr = cam_pipeline_qr_create(&qr_cfg);
    if (!qr) {
        ESP_LOGE(TAG, "QR consumer creation failed");
    }

    board_backlight_set(100);

    ESP_LOGI(TAG, "QR decoder running (%"PRIu32"x%"PRIu32" square)", square, square);

    /* Delayed log dump — waits for USB serial to reconnect, then dumps
     * the complete boot log from flash so nothing is missed.
     * Must use internal-RAM stack since board_log_flash_dump reads flash. */
    {
        StaticTask_t *dump_tcb = heap_caps_malloc(sizeof(StaticTask_t),
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        StackType_t *dump_stack = heap_caps_malloc(4096 * sizeof(StackType_t),
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (dump_tcb && dump_stack) {
            xTaskCreateStatic(log_dump_task, "log_dump", 4096, NULL, 1,
                              dump_stack, dump_tcb);
        }
    }

    board_run();
}
