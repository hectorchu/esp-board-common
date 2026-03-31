#pragma once

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise ST7701 MIPI-DSI display.
 *
 * Sets up DSI bus, DBI command channel, DPI video panel, and the
 * ST7701 vendor init sequence.  Uses espressif/esp_lcd_st7701.
 *
 * @param io_handle   Output: set to NULL (no separate IO for MIPI-DSI LVGL path)
 * @param panel_handle Output: DPI panel handle (wraps ST7701 driver)
 */
void board_display_st7701_init(esp_lcd_panel_io_handle_t *io_handle,
                               esp_lcd_panel_handle_t *panel_handle);

#ifdef __cplusplus
}
#endif
