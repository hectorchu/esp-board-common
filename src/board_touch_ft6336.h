#pragma once

#include "driver/i2c_master.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise FT6336 (FT5x06 family) touch controller via I2C.
 * Uses espressif/esp_lcd_touch_ft5x06 component from registry.
 */
esp_lcd_touch_handle_t board_touch_ft6336_init(i2c_master_bus_handle_t bus,
                                                uint16_t x_max, uint16_t y_max);

#ifdef __cplusplus
}
#endif
