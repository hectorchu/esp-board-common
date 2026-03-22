#pragma once

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise AXS15231B QSPI display.
 *
 * Sets up SPI bus with 4 data lines, creates the panel driver with
 * vendor-specific init commands, resets and initialises the panel
 * (display initially OFF — turned on by LVGL port).
 */
void board_display_axs15231b_init(esp_lcd_panel_io_handle_t *io_handle,
                                   esp_lcd_panel_handle_t *panel_handle,
                                   size_t max_transfer_sz);

#ifdef __cplusplus
}
#endif
