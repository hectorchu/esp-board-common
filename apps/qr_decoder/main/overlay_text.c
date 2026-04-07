/**
 * Direct text overlay for SPI dummy-draw displays.
 *
 * Renders LVGL font glyphs into gap-area buffers above/below the camera feed,
 * then writes directly to the panel via esp_lcd_panel_draw_bitmap().
 * The camera frame is never modified — text lives only in the black gaps.
 *
 * See overlay_text.h for API docs.
 */
#include "overlay_text.h"

#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"

static const char *TAG = "overlay_text";

#define QR_TEXT_MAX        512
#define FPS_TEXT_MAX       80
#define TEXT_PAD           8    /* px padding from edge */
#define DMA_STRIPE_LINES   80   /* lines per DMA transfer — sized to cover full gap in one shot */
#define DMA_BUF_ALIGN      64

/* ── Context ── */

struct overlay_text {
    /* Panel geometry */
    esp_lcd_panel_handle_t panel;
    uint32_t panel_w, panel_h;
    uint32_t gap_top;           /* height of top gap (rows 0..gap_top-1) */
    uint32_t gap_bot;           /* height of bottom gap */
    uint32_t cam_y;             /* panel row where camera starts = gap_top */
    bool byte_swap;
    const lv_font_t *font;

    /* Gap-area RGB565 buffers (PSRAM) */
    uint16_t *top_buf;          /* panel_w × gap_top */
    uint16_t *bot_buf;          /* panel_w × gap_bot */

    /* DMA stripe buffer for byte-swapped panel writes (internal RAM) */
    uint8_t *dma_buf;
    uint32_t dma_stripe_lines;

    /* Thread-safe text state */
    portMUX_TYPE lock;
    char qr_text[QR_TEXT_MAX];
    volatile bool qr_visible;
    int64_t qr_hide_time;       /* esp_timer µs, 0 = no timeout */
    char fps_text[FPS_TEXT_MAX];
    volatile bool fps_visible;

    /* Dirty flags — set by setters, consumed by overlay_cb (camera task).
     * All panel writes happen from the camera task to avoid SPI bus contention. */
    volatile bool fps_dirty;
    volatile bool qr_dirty;

    /* Landscape mode: text rendered in landscape orientation, rotated 90° CCW
     * to portrait panel coords.  Gap portions written to panel; camera-area
     * portions composited onto frame_buf per-frame (black-keyed). */
    bool landscape;
    uint16_t *fps_land_buf;     /* landscape render buffer (PSRAM) */
    uint16_t *fps_rot_buf;      /* rotated portrait buffer (PSRAM) */
    uint32_t fps_land_w, fps_land_h;
    uint32_t fps_rot_px, fps_rot_py;   /* portrait position of rotated buf */

    uint16_t *qr_land_buf;      /* landscape render buffer (PSRAM) */
    uint16_t *qr_rot_buf;       /* rotated portrait buffer (PSRAM) */
    uint32_t qr_land_w, qr_land_h;
    uint32_t qr_rot_px, qr_rot_py;    /* portrait position of rotated buf */
};

/* ── RGB565 helpers ── */

static inline uint16_t rgb565_pack(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (b >> 3);
}

static inline uint16_t alpha_blend_rgb565(uint16_t color, uint8_t alpha)
{
    if (alpha == 0) return 0;
    if (alpha >= 248) return color;

    uint32_t r = ((color >> 11) & 0x1F) * alpha;
    uint32_t g = ((color >> 5)  & 0x3F) * alpha;
    uint32_t b = ( color        & 0x1F) * alpha;

    uint16_t out = (uint16_t)(((r >> 8) << 11) | ((g >> 8) << 5) | (b >> 8));
    return out ? out : 1;
}

static inline void copy_swap_u16(uint16_t *dst, const uint16_t *src, size_t count)
{
    while (count--) {
        uint16_t p = *src++;
        *dst++ = (uint16_t)((p << 8) | (p >> 8));
    }
}

/* ── Raw glyph bitmap decoder ── */

static uint8_t glyph_alpha(const uint8_t *bmp, uint8_t bpp,
                            uint16_t box_w, uint16_t stride,
                            uint16_t px, uint16_t py)
{
    uint32_t byte_idx;
    uint8_t raw;

    /* stride == 0: flat continuous stream of sub-byte values, no per-row padding.
     * stride != 0: each row is padded to `stride` bytes. */

    switch (bpp) {
    case 8:
        return bmp[stride ? (py * stride + px) : ((uint32_t)py * box_w + px)];

    case 4: {
        uint32_t nibble = stride
            ? (uint32_t)py * stride * 2 + px
            : (uint32_t)py * box_w + px;
        byte_idx = nibble / 2;
        raw = (nibble & 1) ? (bmp[byte_idx] & 0x0F) : (bmp[byte_idx] >> 4);
        return (uint8_t)(raw * 17);
    }

    case 2: {
        uint32_t pair = stride
            ? (uint32_t)py * stride * 4 + px
            : (uint32_t)py * box_w + px;
        byte_idx = pair / 4;
        raw = (bmp[byte_idx] >> (6 - (pair % 4) * 2)) & 0x03;
        return (uint8_t)(raw * 85);
    }

    case 1: {
        uint32_t bit = stride
            ? (uint32_t)py * stride * 8 + px
            : (uint32_t)py * box_w + px;
        byte_idx = bit / 8;
        raw = (bmp[byte_idx] >> (7 - (bit % 8))) & 0x01;
        return raw ? 255 : 0;
    }

    default:
        return 0;
    }
}

/* ── Text renderer ──
 * Renders a string into an arbitrary RGB565 buffer.
 * Returns Y coordinate one line below the last rendered line. */

static int render_text_buf(uint16_t *buf, uint32_t buf_w, uint32_t buf_h,
                           const lv_font_t *font, const char *text,
                           uint16_t color, int start_x, int start_y,
                           int max_width)
{
    int32_t line_h = font->line_height;
    int32_t baseline_ofs = line_h - font->base_line;
    int cursor_x = start_x;
    int cursor_y = start_y;
    int right_edge = start_x + max_width;

    const char *p = text;
    while (*p) {
        uint32_t letter = (uint8_t)*p++;

        if (letter == '\n') {
            cursor_x = start_x;
            cursor_y += line_h;
            continue;
        }
        if (letter < 0x20) continue;

        uint32_t next_letter = *p ? (uint8_t)*p : 0;

        lv_font_glyph_dsc_t g;
        memset(&g, 0, sizeof(g));
        if (!lv_font_get_glyph_dsc(font, &g, letter, next_letter))
            continue;

        int adv = g.adv_w;

        if (cursor_x + adv > right_edge && cursor_x > start_x) {
            cursor_x = start_x;
            cursor_y += line_h;
        }
        if (cursor_y + line_h > (int)buf_h) break;

        g.req_raw_bitmap = 1;
        const uint8_t *bmp = (const uint8_t *)g.resolved_font->get_glyph_bitmap(&g, NULL);

        if (bmp && g.box_w > 0 && g.box_h > 0) {
            uint8_t bpp = g.format;
            if (bpp == 0) bpp = 4;

            int gx = cursor_x + g.ofs_x;
            int gy = cursor_y + baseline_ofs - g.ofs_y - g.box_h;

            for (int py = 0; py < g.box_h; py++) {
                int fy = gy + py;
                if (fy < 0) continue;
                if (fy >= (int)buf_h) break;

                for (int px = 0; px < g.box_w; px++) {
                    int fx = gx + px;
                    if (fx < 0) continue;
                    if (fx >= (int)buf_w) break;

                    uint8_t a = glyph_alpha(bmp, bpp, g.box_w, g.stride, px, py);
                    if (a == 0) continue;

                    buf[fy * buf_w + fx] = alpha_blend_rgb565(color, a);
                }
            }
        }

        cursor_x += adv;
        lv_font_glyph_release_draw_data(&g);
    }

    return cursor_y + line_h;
}

/* ── Panel write — byte-swap + striped DMA blit ── */

static void write_gap_to_panel(overlay_text_t *ov, const uint16_t *gap_buf,
                                uint32_t gap_h, uint32_t panel_y_start)
{
    if (!ov->byte_swap) {
        /* No byte-swap needed (e.g., MIPI-DSI) — single draw call */
        esp_lcd_panel_draw_bitmap(ov->panel, 0, panel_y_start,
                                  ov->panel_w, panel_y_start + gap_h,
                                  gap_buf);
        return;
    }

    /* SPI panels: byte-swap through internal-RAM DMA buffer in stripes */
    uint32_t row = 0;
    while (row < gap_h) {
        uint32_t block = gap_h - row;
        if (block > ov->dma_stripe_lines) block = ov->dma_stripe_lines;

        const uint16_t *src = gap_buf + (size_t)row * ov->panel_w;
        copy_swap_u16((uint16_t *)ov->dma_buf, src, (size_t)ov->panel_w * block);

        uint32_t y = panel_y_start + row;
        esp_lcd_panel_draw_bitmap(ov->panel, 0, y,
                                  ov->panel_w, y + block, ov->dma_buf);
        row += block;
    }
}

/* ── 90° CW rotation for RGB565 buffers ── */

static void rotate_90cw_rgb565(const uint16_t *src, uint32_t src_w, uint32_t src_h,
                               uint16_t *dst)
{
    /* dst dimensions: src_h wide × src_w tall */
    for (uint32_t y = 0; y < src_h; y++) {
        for (uint32_t x = 0; x < src_w; x++) {
            dst[x * src_h + (src_h - 1 - y)] = src[y * src_w + x];
        }
    }
}

/* ── Partial-width panel write (landscape gap strips) ── */

static void write_rect_to_panel(overlay_text_t *ov,
                                const uint16_t *buf, uint32_t buf_w,
                                uint32_t rect_h,
                                uint32_t panel_x, uint32_t panel_y)
{
    if (!ov->byte_swap) {
        esp_lcd_panel_draw_bitmap(ov->panel, panel_x, panel_y,
                                  panel_x + buf_w, panel_y + rect_h, buf);
        return;
    }

    /* SPI: byte-swap through DMA buffer in stripes */
    uint32_t row = 0;
    while (row < rect_h) {
        uint32_t block = rect_h - row;
        if (block > ov->dma_stripe_lines) block = ov->dma_stripe_lines;

        const uint16_t *src = buf + (size_t)row * buf_w;
        copy_swap_u16((uint16_t *)ov->dma_buf, src, (size_t)buf_w * block);

        uint32_t y = panel_y + row;
        esp_lcd_panel_draw_bitmap(ov->panel, panel_x, y,
                                  panel_x + buf_w, y + block, ov->dma_buf);
        row += block;
    }
}

/* ── Black-keyed composite: nonzero overlay pixels overwrite frame ── */

static void composite_black_keyed(uint16_t *frame_buf, uint32_t frame_w,
                                  const uint16_t *overlay, uint32_t ov_w,
                                  uint32_t ov_h,
                                  uint32_t dst_x, uint32_t dst_y)
{
    for (uint32_t y = 0; y < ov_h; y++) {
        uint16_t *dst = frame_buf + (dst_y + y) * frame_w + dst_x;
        const uint16_t *src = overlay + y * ov_w;
        for (uint32_t x = 0; x < ov_w; x++) {
            if (src[x] != 0)
                dst[x] = src[x];
        }
    }
}

/* ── Render + write a gap area ── */

static void update_top_gap(overlay_text_t *ov, const char *fps_text)
{
    memset(ov->top_buf, 0, (size_t)ov->panel_w * ov->gap_top * sizeof(uint16_t));

    if (fps_text && fps_text[0]) {
        render_text_buf(ov->top_buf, ov->panel_w, ov->gap_top,
                        ov->font, fps_text,
                        rgb565_pack(255, 255, 255),
                        TEXT_PAD, TEXT_PAD,
                        ov->panel_w - 2 * TEXT_PAD);
    }

    write_gap_to_panel(ov, ov->top_buf, ov->gap_top, 0);
}

static void update_bot_gap(overlay_text_t *ov, const char *qr_text, bool visible)
{
    memset(ov->bot_buf, 0, (size_t)ov->panel_w * ov->gap_bot * sizeof(uint16_t));

    if (visible && qr_text && qr_text[0]) {
        render_text_buf(ov->bot_buf, ov->panel_w, ov->gap_bot,
                        ov->font, qr_text,
                        rgb565_pack(0, 255, 0),
                        TEXT_PAD, TEXT_PAD,
                        ov->panel_w - 2 * TEXT_PAD);
    }

    uint32_t bot_y = ov->cam_y + (ov->panel_h - ov->gap_top - ov->gap_bot);
    write_gap_to_panel(ov, ov->bot_buf, ov->gap_bot, bot_y);
}

/* ── Landscape update functions ── */

static void update_fps_landscape(overlay_text_t *ov, const char *fps_text)
{
    /* Render text in landscape orientation, rotate 90° CCW, write to gap */
    memset(ov->fps_land_buf, 0,
           (size_t)ov->fps_land_w * ov->fps_land_h * sizeof(uint16_t));

    if (fps_text && fps_text[0]) {
        render_text_buf(ov->fps_land_buf, ov->fps_land_w, ov->fps_land_h,
                        ov->font, fps_text,
                        rgb565_pack(255, 255, 255),
                        TEXT_PAD, TEXT_PAD,
                        ov->fps_land_w - 2 * TEXT_PAD);
    }

    rotate_90cw_rgb565(ov->fps_land_buf, ov->fps_land_w, ov->fps_land_h,
                        ov->fps_rot_buf);

    /* Rotated buf is fps_land_h wide × fps_land_w tall — fully within top gap */
    write_rect_to_panel(ov, ov->fps_rot_buf, ov->fps_land_h,
                        ov->fps_land_w,   /* rect height = landscape width */
                        ov->fps_rot_px, ov->fps_rot_py);
}

static void update_qr_landscape(overlay_text_t *ov, const char *qr_text, bool visible)
{
    /* Render text in landscape orientation, rotate 90° CCW */
    memset(ov->qr_land_buf, 0,
           (size_t)ov->qr_land_w * ov->qr_land_h * sizeof(uint16_t));

    if (visible && qr_text && qr_text[0]) {
        render_text_buf(ov->qr_land_buf, ov->qr_land_w, ov->qr_land_h,
                        ov->font, qr_text,
                        rgb565_pack(0, 255, 0),
                        TEXT_PAD, TEXT_PAD,
                        ov->qr_land_w - 2 * TEXT_PAD);
    }

    rotate_90cw_rgb565(ov->qr_land_buf, ov->qr_land_w, ov->qr_land_h,
                        ov->qr_rot_buf);

    /* Rotated buf is qr_land_h wide × qr_land_w tall, spans full panel height.
     * Write the gap portions to the panel now. Camera portion composited per-frame. */
    uint32_t rot_w = ov->qr_land_h;   /* rotated width */
    uint32_t px = ov->qr_rot_px;

    /* Top gap: rotated rows 0..gap_top-1 */
    write_rect_to_panel(ov, ov->qr_rot_buf,
                        rot_w, ov->gap_top,
                        px, 0);

    /* Bottom gap: rotated rows (panel_h - gap_bot)..panel_h-1 */
    uint32_t bot_y = ov->panel_h - ov->gap_bot;
    const uint16_t *bot_src = ov->qr_rot_buf + (size_t)bot_y * rot_w;
    write_rect_to_panel(ov, bot_src,
                        rot_w, ov->gap_bot,
                        px, bot_y);
}

/* ── Public API ── */

overlay_text_t *overlay_text_create(const overlay_text_config_t *cfg)
{
    if (!cfg || !cfg->panel_handle || !cfg->font) {
        ESP_LOGE(TAG, "Invalid config");
        return NULL;
    }

    uint32_t gap_top = (cfg->panel_height - cfg->camera_height) / 2;
    uint32_t gap_bot = cfg->panel_height - cfg->camera_height - gap_top;

    if (gap_top == 0 && gap_bot == 0) {
        ESP_LOGW(TAG, "No gap areas — camera fills entire panel");
        return NULL;
    }

    overlay_text_t *ov = calloc(1, sizeof(overlay_text_t));
    if (!ov) {
        ESP_LOGE(TAG, "Failed to allocate context");
        return NULL;
    }

    ov->panel = (esp_lcd_panel_handle_t)cfg->panel_handle;
    ov->panel_w = cfg->panel_width;
    ov->panel_h = cfg->panel_height;
    ov->gap_top = gap_top;
    ov->gap_bot = gap_bot;
    ov->cam_y = gap_top;
    ov->byte_swap = cfg->byte_swap;
    ov->font = cfg->font;
    ov->landscape = cfg->landscape;
    portMUX_INITIALIZE(&ov->lock);

    if (cfg->landscape) {
        /* ── Landscape buffer allocation ──
         * FPS: 80w × 130h landscape → 130w × 80h rotated (gap-only)
         * QR:  480w × 56h landscape → 56w × 480h rotated (full panel height) */
        ov->fps_land_w = gap_top;   /* 80 — landscape width = gap size */
        ov->fps_land_h = 80;        /* 4 lines of montserrat_14 (~16px each) + padding */
        ov->qr_land_w = cfg->panel_height;  /* 480 — full landscape width */
        ov->qr_land_h = 40;         /* 2 lines of montserrat_14 + padding */

        /* FPS rotated position: landscape right (portrait bottom gap) */
        ov->fps_rot_px = cfg->panel_width - ov->fps_land_h - TEXT_PAD;
        ov->fps_rot_py = cfg->panel_height - gap_bot;  /* bottom gap */

        /* QR rotated position: near landscape bottom (portrait left side) */
        ov->qr_rot_px = TEXT_PAD;
        ov->qr_rot_py = 0;   /* spans full panel height */

        size_t fps_px = (size_t)ov->fps_land_w * ov->fps_land_h;
        size_t qr_px  = (size_t)ov->qr_land_w  * ov->qr_land_h;

        ov->fps_land_buf = heap_caps_calloc(1, fps_px * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        ov->fps_rot_buf  = heap_caps_calloc(1, fps_px * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        ov->qr_land_buf  = heap_caps_calloc(1, qr_px * 2,  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        ov->qr_rot_buf   = heap_caps_calloc(1, qr_px * 2,  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

        if (!ov->fps_land_buf || !ov->fps_rot_buf ||
            !ov->qr_land_buf  || !ov->qr_rot_buf) {
            ESP_LOGE(TAG, "Failed to allocate landscape buffers");
            heap_caps_free(ov->fps_land_buf);
            heap_caps_free(ov->fps_rot_buf);
            heap_caps_free(ov->qr_land_buf);
            heap_caps_free(ov->qr_rot_buf);
            free(ov);
            return NULL;
        }
    } else {
        /* ── Portrait gap buffer allocation ── */
        if (gap_top > 0) {
            size_t top_size = (size_t)cfg->panel_width * gap_top * sizeof(uint16_t);
            ov->top_buf = heap_caps_calloc(1, top_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!ov->top_buf) {
                ESP_LOGE(TAG, "Failed to allocate top gap buffer (%zu bytes)", top_size);
                free(ov);
                return NULL;
            }
        }

        if (gap_bot > 0) {
            size_t bot_size = (size_t)cfg->panel_width * gap_bot * sizeof(uint16_t);
            ov->bot_buf = heap_caps_calloc(1, bot_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!ov->bot_buf) {
                ESP_LOGE(TAG, "Failed to allocate bottom gap buffer (%zu bytes)", bot_size);
                heap_caps_free(ov->top_buf);
                free(ov);
                return NULL;
            }
        }
    }

    /* Allocate DMA stripe buffer in internal RAM (for SPI byte-swap) */
    if (cfg->byte_swap) {
        ov->dma_stripe_lines = DMA_STRIPE_LINES;
        size_t stripe_size = (size_t)cfg->panel_width * DMA_STRIPE_LINES * sizeof(uint16_t);
        ov->dma_buf = heap_caps_aligned_alloc(DMA_BUF_ALIGN, stripe_size,
                                               MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (!ov->dma_buf) {
            ESP_LOGE(TAG, "Failed to allocate DMA stripe buffer");
            heap_caps_free(ov->bot_buf);
            heap_caps_free(ov->top_buf);
            heap_caps_free(ov->fps_land_buf);
            heap_caps_free(ov->fps_rot_buf);
            heap_caps_free(ov->qr_land_buf);
            heap_caps_free(ov->qr_rot_buf);
            free(ov);
            return NULL;
        }
    }

    ESP_LOGI(TAG, "Created: panel %"PRIu32"x%"PRIu32", gaps %"PRIu32"+%"PRIu32
             "px, font h=%"PRId32", %s",
             cfg->panel_width, cfg->panel_height,
             gap_top, gap_bot, cfg->font->line_height,
             cfg->landscape ? "landscape" : "portrait");
    return ov;
}

void overlay_text_set_qr(overlay_text_t *ov, const char *text,
                          uint32_t timeout_ms)
{
    if (!ov) return;

    taskENTER_CRITICAL(&ov->lock);
    strncpy(ov->qr_text, text, QR_TEXT_MAX - 1);
    ov->qr_text[QR_TEXT_MAX - 1] = '\0';
    ov->qr_visible = true;
    taskEXIT_CRITICAL(&ov->lock);

    ov->qr_hide_time = (timeout_ms > 0)
        ? esp_timer_get_time() + (int64_t)timeout_ms * 1000
        : 0;
    ov->qr_dirty = true;
}

void overlay_text_set_fps(overlay_text_t *ov, const char *text)
{
    if (!ov) return;

    taskENTER_CRITICAL(&ov->lock);
    if (text && text[0]) {
        strncpy(ov->fps_text, text, FPS_TEXT_MAX - 1);
        ov->fps_text[FPS_TEXT_MAX - 1] = '\0';
        ov->fps_visible = true;
    } else {
        ov->fps_text[0] = '\0';
        ov->fps_visible = false;
    }
    taskEXIT_CRITICAL(&ov->lock);

    ov->fps_dirty = true;
}

void overlay_text_cb(uint8_t *frame_buf, uint32_t width,
                     uint32_t height, void *user_ctx)
{
    overlay_text_t *ov = (overlay_text_t *)user_ctx;
    if (!ov) return;

    /* All panel writes happen here (camera task) to avoid SPI bus contention
     * with push_frame_dummy_draw which also runs in this task. */

    /* Auto-hide QR text */
    if (ov->qr_visible && ov->qr_hide_time > 0) {
        if (esp_timer_get_time() >= ov->qr_hide_time) {
            ov->qr_visible = false;
            ov->qr_hide_time = 0;
            ov->qr_dirty = true;
        }
    }

    if (ov->landscape) {
        /* ── Landscape path ── */

        if (ov->fps_dirty) {
            ov->fps_dirty = false;
            char snap[FPS_TEXT_MAX];
            taskENTER_CRITICAL(&ov->lock);
            memcpy(snap, ov->fps_text, FPS_TEXT_MAX);
            taskEXIT_CRITICAL(&ov->lock);
            update_fps_landscape(ov, snap);
        }

        if (ov->qr_dirty) {
            ov->qr_dirty = false;
            char snap[QR_TEXT_MAX];
            bool vis;
            taskENTER_CRITICAL(&ov->lock);
            memcpy(snap, ov->qr_text, QR_TEXT_MAX);
            vis = ov->qr_visible;
            taskEXIT_CRITICAL(&ov->lock);
            update_qr_landscape(ov, snap, vis);
        }

        /* Per-frame: composite QR camera-area strip onto frame_buf.
         * The rotated QR buffer spans the full panel height; the camera
         * portion is rows gap_top..(panel_h - gap_bot - 1). */
        if (ov->qr_visible && frame_buf) {
            uint32_t rot_w = ov->qr_land_h;  /* rotated width */
            uint32_t cam_rows = ov->panel_h - ov->gap_top - ov->gap_bot;
            const uint16_t *cam_src = ov->qr_rot_buf + (size_t)ov->gap_top * rot_w;
            composite_black_keyed((uint16_t *)frame_buf, width,
                                  cam_src, rot_w, cam_rows,
                                  ov->qr_rot_px, 0);
        }
    } else {
        /* ── Portrait path ── */

        if (ov->fps_dirty) {
            ov->fps_dirty = false;
            char snap[FPS_TEXT_MAX];
            taskENTER_CRITICAL(&ov->lock);
            memcpy(snap, ov->fps_text, FPS_TEXT_MAX);
            taskEXIT_CRITICAL(&ov->lock);
            update_top_gap(ov, snap);
        }

        if (ov->qr_dirty) {
            ov->qr_dirty = false;
            char snap[QR_TEXT_MAX];
            bool vis;
            taskENTER_CRITICAL(&ov->lock);
            memcpy(snap, ov->qr_text, QR_TEXT_MAX);
            vis = ov->qr_visible;
            taskEXIT_CRITICAL(&ov->lock);
            update_bot_gap(ov, snap, vis);
        }
    }
}
