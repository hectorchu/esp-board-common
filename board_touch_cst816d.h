#pragma once

#include "driver/i2c_master.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise CST816D (CST816S family) touch controller via I2C.
 * Uses espressif/esp_lcd_touch_cst816s component from registry.
 */
esp_lcd_touch_handle_t board_touch_cst816d_init(i2c_master_bus_handle_t bus,
                                                  uint16_t x_max, uint16_t y_max);

#ifdef __cplusplus
}
#endif
