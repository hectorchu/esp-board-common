/**
 * Direct text overlay for SPI dummy-draw displays.
 *
 * One of four text overlay paths in the QR decoder (see
 * docs/knowledge/text-overlay-architecture.md for the full picture):
 *   1. DSI landscape  — pre-rendered rotated canvas + bbox crop
 *   2. SPI portrait   — THIS MODULE: direct font render into panel gap areas
 *   3. DSI portrait   — standard LVGL label widgets
 *   4. SPI landscape  — future (ARGB compositing)
 *
 * Why direct rendering instead of LVGL widgets?
 *   SPI dummy-draw mode calls lvgl_port_stop() to eliminate tearing.
 *   With LVGL halted, no widgets render and no timers fire.
 *
 * Why gap areas instead of compositing onto the camera feed?
 *   RGB565 overlay has no true alpha — edges blend against black, creating
 *   dark fringes on bright camera backgrounds.  Gap-area text on a black
 *   background looks clean and avoids per-frame composite overhead.
 *
 * Why deferred panel writes (dirty flags + overlay callback)?
 *   All esp_lcd_panel_draw_bitmap() calls share the SPI bus.  Writing from
 *   multiple tasks causes bus contention that freezes the display.  All
 *   panel writes happen from the camera task via the overlay callback.
 *
 * Thread-safe: text setters can be called from any task (QR decode, timers).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Forward-declare to avoid requiring lvgl.h in consumers */
typedef struct _lv_font_t lv_font_t;

typedef struct overlay_text overlay_text_t;

/**
 * Configuration for overlay text — describes the panel layout.
 *
 * The camera feed occupies a centered square on the panel.  Text renders
 * in the gap areas above (FPS stats) and below (QR result) the camera.
 *
 *   ┌─────────────────┐  ← y=0
 *   │   FPS stats      │  top gap (gap_top px)
 *   ├─────────────────┤  ← y=gap_top
 *   │                  │
 *   │   camera feed    │  camera_height px
 *   │                  │
 *   ├─────────────────┤  ← y=gap_top + camera_height
 *   │   QR result      │  bottom gap
 *   └─────────────────┘  ← y=panel_height
 */
typedef struct {
    uint32_t panel_width;       /**< Panel width in pixels (e.g., 320) */
    uint32_t panel_height;      /**< Panel height in pixels (e.g., 480) */
    uint32_t camera_height;     /**< Camera square height (e.g., 320) */
    bool byte_swap;             /**< true for SPI panels (RGB565 byte swap) */
    bool landscape;             /**< true: render text rotated 90° for landscape viewing */
    void *panel_handle;         /**< esp_lcd_panel_handle_t */
    const lv_font_t *font;     /**< LVGL font for rendering */
} overlay_text_config_t;

/**
 * Create overlay text context.
 * Allocates gap-area buffers in PSRAM and a DMA stripe buffer in internal RAM.
 *
 * @param cfg  Panel and font configuration
 * @return Context handle, or NULL on allocation failure
 */
overlay_text_t *overlay_text_create(const overlay_text_config_t *cfg);

/**
 * Set QR result text.  Thread-safe — called from QR decode task.
 * Renders text in the bottom gap area and writes to the panel immediately.
 * Auto-hides after timeout_ms (gap cleared to black).
 *
 * @param ov         Overlay context
 * @param text       Text to display (copied internally, max 511 chars)
 * @param timeout_ms Auto-hide timeout in ms (0 = no auto-hide)
 */
void overlay_text_set_qr(overlay_text_t *ov, const char *text,
                          uint32_t timeout_ms);

/**
 * Set FPS stats text.  Thread-safe.
 * Renders in the top gap area and writes to the panel immediately.
 * Pass NULL or "" to clear.
 */
void overlay_text_set_fps(overlay_text_t *ov, const char *text);

/**
 * Pipeline overlay callback — handles QR auto-hide timing.
 * Register as pipeline_overlay_cb_t.  Does NOT modify the camera frame.
 * Only checks whether the QR text auto-hide timer has expired.
 */
void overlay_text_cb(uint8_t *frame_buf, uint32_t width,
                     uint32_t height, void *user_ctx);
