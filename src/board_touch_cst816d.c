#include "board.h"
#include "board_config.h"

#if BOARD_TOUCH_DRIVER == TOUCH_CST816D

#include "board_touch_cst816d.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_log.h"

esp_lcd_touch_handle_t board_touch_cst816d_init(i2c_master_bus_handle_t bus,
                                                  uint16_t x_max, uint16_t y_max)
{
    esp_lcd_panel_io_handle_t touch_io_handle;
    esp_lcd_panel_io_i2c_config_t touch_io_config = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    touch_io_config.scl_speed_hz = 400000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus, &touch_io_config, &touch_io_handle));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = x_max,
        .y_max = y_max,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    esp_lcd_touch_handle_t touch_handle;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst816s(touch_io_handle, &tp_cfg, &touch_handle));
    return touch_handle;
}

#endif /* BOARD_TOUCH_DRIVER == TOUCH_CST816D */
