/**
 * Rotated text overlay for DSI landscape displays.
 *
 * See overlay_rotated.h for API docs and design rationale.
 */
#include "overlay_rotated.h"

#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "overlay_rotated";

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

/* ── Public API ── */

bool rotated_overlay_init(rotated_overlay_t *ov, lv_obj_t *parent,
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
        ESP_LOGE(TAG, "Failed to alloc %dx%d src canvas (%zu bytes)",
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
        ESP_LOGE(TAG, "Failed to alloc rotated buf (%zu bytes)", buf_size);
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

void rotated_overlay_update(rotated_overlay_t *ov, const char *text,
                            lv_color_t color, const lv_font_t *font)
{
    lv_canvas_fill_bg(ov->canvas_src, lv_color_black(),
                      ov->opaque ? LV_OPA_COVER : LV_OPA_TRANSP);

    lv_layer_t layer;
    lv_canvas_init_layer(ov->canvas_src, &layer);
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.color = color;
    dsc.font = font;
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

void rotated_overlay_show(rotated_overlay_t *ov, bool visible)
{
    if (visible)
        lv_obj_clear_flag(ov->img_rotated, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(ov->img_rotated, LV_OBJ_FLAG_HIDDEN);
}
