/**
 * Camera pipeline DVP driver for ESP32-S3.
 *
 * Uses the esp32-camera component for DVP capture. The capture task
 * loops on esp_camera_fb_get(), byte-swaps RGB565 from big-endian
 * to little-endian, then feeds frames to the pipeline via callback.
 *
 * TODO: Full implementation — Phase 6 of the pipeline integration.
 */
#include "board.h"
#include "board_config.h"
#include "board_camera.h"
#include "overlay_util.h"

#if BOARD_HAS_CAMERA && BOARD_CAMERA_INTERFACE == CAMERA_DVP

#include "board_pipeline_camera_dvp.h"
#include "esp_log.h"

static const char *TAG = "pipeline_cam_dvp";

#define DVP_TASK_STACK     (16 * 1024)
#define DVP_TASK_PRIORITY  5

typedef struct {
    uint16_t frame_w;
    uint16_t frame_h;
    cam_pipeline_frame_cb_t frame_cb;
    void *user_ctx;
    TaskHandle_t task_handle;
    volatile bool running;
} dvp_driver_ctx_t;

static void dvp_capture_task(void *param)
{
    dvp_driver_ctx_t *ctx = (dvp_driver_ctx_t *)param;

    ESP_LOGI(TAG, "Capture task started (%dx%d)", ctx->frame_w, ctx->frame_h);

    uint8_t *buf = malloc(ctx->frame_w * ctx->frame_h * 2);

    while (ctx->running) {
        board_camera_frame_t frame;
        esp_err_t err = board_camera_fb_get(&frame, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "board_camera_fb_get failed: %s", esp_err_to_name(err));
            break;
        }

        copy_swap_u16((uint16_t*)buf, (uint16_t*)frame.buf, frame.len / 2);
        ctx->frame_cb(buf, frame.width, frame.height, ctx->user_ctx);

        board_camera_fb_return(&frame);
    }

    free(buf);

    ESP_LOGI(TAG, "Capture task exiting");
    vTaskDelete(NULL);
}

static void *dvp_init(const void *platform_config)
{
    const board_pipeline_dvp_config_t *cfg =
        (const board_pipeline_dvp_config_t *)platform_config;

    dvp_driver_ctx_t *ctx = calloc(1, sizeof(dvp_driver_ctx_t));
    if (!ctx) return NULL;

    ctx->frame_w = cfg->frame_width;
    ctx->frame_h = cfg->frame_height;

    board_camera_init(NULL, cfg->frame_width, cfg->frame_height);

    return ctx;
}

static esp_err_t dvp_start(void *handle, cam_pipeline_frame_cb_t frame_cb,
                           void *user_ctx, int core_id)
{
    dvp_driver_ctx_t *ctx = (dvp_driver_ctx_t *)handle;
    ctx->frame_cb = frame_cb;
    ctx->user_ctx = user_ctx;
    ctx->running = true;

    /* Spawn capture task */
    BaseType_t ret = xTaskCreatePinnedToCore(
        dvp_capture_task, "dvp_cap", DVP_TASK_STACK, ctx,
        DVP_TASK_PRIORITY, &ctx->task_handle, core_id);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create capture task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Streaming started (capture task on core %d)", core_id);
    return ESP_OK;
}

static esp_err_t dvp_stop(void *handle)
{
    dvp_driver_ctx_t *ctx = (dvp_driver_ctx_t *)handle;
    ctx->running = false;

    /* Wait for capture task to exit */
    if (ctx->task_handle) {
        /* Give the task time to notice and exit */
        vTaskDelay(pdMS_TO_TICKS(100));
        ctx->task_handle = NULL;
    }

    ESP_LOGI(TAG, "Streaming stopped");
    return ESP_OK;
}

static void dvp_deinit(void *handle)
{
    dvp_driver_ctx_t *ctx = (dvp_driver_ctx_t *)handle;
    if (!ctx) return;

    free(ctx);

    ESP_LOGI(TAG, "DVP driver deinitialized");
}

static esp_err_t dvp_get_resolution(void *handle, uint32_t *width,
                                    uint32_t *height)
{
    dvp_driver_ctx_t *ctx = (dvp_driver_ctx_t *)handle;
    if (!ctx) return ESP_ERR_INVALID_STATE;
    *width = ctx->frame_w;
    *height = ctx->frame_h;
    return ESP_OK;
}

const cam_pipeline_camera_driver_t board_pipeline_dvp_driver = {
    .init           = dvp_init,
    .start          = dvp_start,
    .stop           = dvp_stop,
    .deinit         = dvp_deinit,
    .get_resolution = dvp_get_resolution,
    .set_ae_target  = NULL,
    .set_focus      = NULL,
    .has_focus_motor = NULL,
};

#endif /* BOARD_HAS_CAMERA && CAMERA_DVP */
