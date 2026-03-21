#pragma once

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise ST7796 SPI display.
 *
 * Standard SPI (single data line), no QSPI workarounds needed.
 * Uses espressif/esp_lcd_st7796 component from registry.
 */
void board_display_st7796_init(esp_lcd_panel_io_handle_t *io_handle,
                                esp_lcd_panel_handle_t *panel_handle,
                                size_t max_transfer_sz);

#ifdef __cplusplus
}
#endif
