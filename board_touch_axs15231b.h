#pragma once

#include "driver/i2c_master.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise AXS15231B touch controller via I2C.
 * Returns an esp_lcd_touch_handle_t for use with LVGL.
 */
esp_lcd_touch_handle_t board_touch_axs15231b_init(i2c_master_bus_handle_t bus,
                                                    uint16_t x_max, uint16_t y_max);

#ifdef __cplusplus
}
#endif
