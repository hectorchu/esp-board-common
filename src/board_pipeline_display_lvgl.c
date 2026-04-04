/**
 * Camera pipeline display driver — LVGL image widget implementation.
 *
 * Renders RGB565 camera frames via an lv_image widget and provides
 * an overlay container for app UI on top of the live feed.
 *
 * push_frame() copies into a persistent PSRAM buffer because the pipeline's
 * frame pointer can be recycled by the next camera callback before LVGL
 * finishes rendering.
 */
#include "board_pipeline_display_lvgl.h"

#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "pipeline_disp_lvgl";

typedef struct {
    lv_obj_t *container;
    lv_obj_t *img_widget;
    lv_image_dsc_t img_dsc;
    uint8_t *cam_buf;
    uint32_t width;
    uint32_t height;
} lvgl_display_ctx_t;

static void *lvgl_display_init(void *parent, uint32_t width, uint32_t height,
                               const void *driver_config)
{
    (void)driver_config;

    lvgl_display_ctx_t *ctx = calloc(1, sizeof(lvgl_display_ctx_t));
    if (!ctx) {
        ESP_LOGE(TAG, "Failed to allocate display context");
        return NULL;
    }

    ctx->width = width;
    ctx->height = height;
    size_t buf_size = width * height * 2; /* RGB565 */

    ctx->cam_buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ctx->cam_buf) {
        ESP_LOGE(TAG, "Failed to allocate %zu byte cam_buf in PSRAM", buf_size);
        free(ctx);
        return NULL;
    }
    memset(ctx->cam_buf, 0, buf_size);

    /* Set up LVGL image descriptor pointing to our persistent buffer */
    ctx->img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    ctx->img_dsc.header.w = width;
    ctx->img_dsc.header.h = height;
    ctx->img_dsc.data_size = buf_size;
    ctx->img_dsc.data = ctx->cam_buf;

    /* Create LVGL widgets — must hold LVGL lock */
    if (!lvgl_port_lock(1000)) {
        ESP_LOGE(TAG, "Failed to acquire LVGL lock for init");
        heap_caps_free(ctx->cam_buf);
        free(ctx);
        return NULL;
    }

    lv_obj_t *par = parent ? (lv_obj_t *)parent : lv_screen_active();

    /* Container fills the parent — overlay widgets are children of this */
    ctx->container = lv_obj_create(par);
    lv_obj_remove_style_all(ctx->container);
    lv_obj_set_size(ctx->container, width, height);
    lv_obj_center(ctx->container);

    /* Image widget fills the container */
    ctx->img_widget = lv_image_create(ctx->container);
    lv_obj_set_size(ctx->img_widget, width, height);
    lv_obj_center(ctx->img_widget);
    lv_image_set_src(ctx->img_widget, &ctx->img_dsc);

    lvgl_port_unlock();

    ESP_LOGI(TAG, "LVGL display driver initialized (%"PRIu32"x%"PRIu32")",
             width, height);
    return ctx;
}

static bool lvgl_display_push_frame(void *handle, const uint8_t *rgb565_buf,
                                    uint32_t width, uint32_t height)
{
    lvgl_display_ctx_t *ctx = (lvgl_display_ctx_t *)handle;
    if (!ctx) return false;

    /* Non-blocking try-lock — skip frame if LVGL is busy */
    if (!lvgl_port_lock(0)) {
        return false;
    }

    memcpy(ctx->cam_buf, rgb565_buf, width * height * 2);
    lv_obj_invalidate(ctx->container);

    lvgl_port_unlock();
    return true;
}

static void lvgl_display_deinit(void *handle)
{
    lvgl_display_ctx_t *ctx = (lvgl_display_ctx_t *)handle;
    if (!ctx) return;

    if (lvgl_port_lock(1000)) {
        if (ctx->container) {
            lv_obj_delete(ctx->container);
        }
        lvgl_port_unlock();
    }

    if (ctx->cam_buf) {
        heap_caps_free(ctx->cam_buf);
    }
    free(ctx);

    ESP_LOGI(TAG, "LVGL display driver deinitialized");
}

static void *lvgl_display_get_overlay_parent(void *handle)
{
    lvgl_display_ctx_t *ctx = (lvgl_display_ctx_t *)handle;
    return ctx ? ctx->container : NULL;
}

const cam_pipeline_display_driver_t board_pipeline_lvgl_display_driver = {
    .init               = lvgl_display_init,
    .push_frame         = lvgl_display_push_frame,
    .deinit             = lvgl_display_deinit,
    .get_overlay_parent = lvgl_display_get_overlay_parent,
};
