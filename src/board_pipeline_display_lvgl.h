/**
 * Camera pipeline display driver using LVGL.
 *
 * Two rendering paths:
 * - Image widget mode (MIPI-DSI): pushes frames via lv_image, supports overlays.
 * - Dummy-draw mode (SPI): bypasses LVGL rendering, writes directly to panel
 *   via esp_lv_adapter_dummy_draw_blit() in DMA-friendly stripes.
 *   Fixes tearing on single-buffered SPI panels (ST7796, etc.).
 */
#pragma once

#include <stdbool.h>
#include "cam_pipeline_display_driver.h"

/**
 * Optional config passed as display_config in cam_pipeline_config_t.
 * If NULL, defaults to image widget mode (no dummy draw).
 */
typedef struct {
    bool use_dummy_draw;    /**< true = bypass LVGL, direct stripe blit to panel */
    bool byte_swap;         /**< true = swap RGB565 bytes (SPI panels only) */
} board_pipeline_lvgl_display_config_t;

extern const cam_pipeline_display_driver_t board_pipeline_lvgl_display_driver;
