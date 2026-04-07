/**
 * Rotated text overlay for DSI landscape displays.
 *
 * Renders text into a hidden LVGL canvas, finds the tight bounding box,
 * CPU-rotates 90° CCW, and displays as a normal lv_image widget.
 * Works around transform_rotation + direct_mode incompatibility.
 *
 * Supports both:
 *   - RGB565 (opaque, black bg) for gap-area overlays — no alpha blend cost
 *   - ARGB8888 (transparent bg) for camera-area overlays
 *
 * Bounding-box crop: only the tight text region is rotated + blended,
 * minimizing CPU and memory overhead.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

typedef struct {
    lv_obj_t *canvas_src;       /**< Hidden canvas for horizontal text rendering */
    lv_obj_t *img_rotated;      /**< Visible image showing rotated result */
    lv_image_dsc_t img_dsc;
    uint8_t *rotated_buf;
    int canvas_w, canvas_h;     /**< Source canvas dimensions */
    lv_color_format_t cf;
    uint8_t bpp;                /**< 2 (RGB565) or 4 (ARGB8888) */
    bool opaque;                /**< true: black bg, false: transparent bg */
    int base_x, base_y;        /**< Position of full (uncropped) rotated image */
} rotated_overlay_t;

/**
 * Initialize a rotated overlay.
 * Allocates canvas + rotated buffer from PSRAM.
 *
 * @param ov        Overlay struct (caller-owned, e.g. static)
 * @param parent    LVGL parent object (typically the active screen)
 * @param canvas_w  Source canvas width (landscape X extent)
 * @param canvas_h  Source canvas height (landscape Y extent)
 * @param cf        Color format: LV_COLOR_FORMAT_RGB565 or LV_COLOR_FORMAT_ARGB8888
 * @param pos_x     Portrait X position for the full (uncropped) rotated image
 * @param pos_y     Portrait Y position for the full (uncropped) rotated image
 * @return true on success
 */
bool rotated_overlay_init(rotated_overlay_t *ov, lv_obj_t *parent,
                          int canvas_w, int canvas_h,
                          lv_color_format_t cf,
                          int pos_x, int pos_y);

/**
 * Render text, bbox-crop, rotate 90° CCW, update display image.
 *
 * @param ov     Overlay context
 * @param text   Text to render
 * @param color  Text color
 * @param font   LVGL font to use
 */
void rotated_overlay_update(rotated_overlay_t *ov, const char *text,
                            lv_color_t color, const lv_font_t *font);

/**
 * Show or hide the rotated overlay image.
 */
void rotated_overlay_show(rotated_overlay_t *ov, bool visible);
