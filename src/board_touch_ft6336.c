#include "board.h"
#include "board_config.h"

#if BOARD_TOUCH_DRIVER == TOUCH_FT6336

#include "board_touch_ft6336.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"

static const char *TAG = "touch_ft6336";

/* FT5x06 scan rate / power mode registers */
#define FT5x06_ID_G_CTRL                (0x86)
#define FT5x06_ID_G_PERIODACTIVE        (0x88)
#define FT5x06_ID_G_PERIODMONITOR       (0x89)

esp_lcd_touch_handle_t board_touch_ft6336_init(i2c_master_bus_handle_t bus,
                                                uint16_t x_max, uint16_t y_max)
{
    esp_lcd_panel_io_handle_t touch_io_handle;
    esp_lcd_panel_io_i2c_config_t touch_io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
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
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(touch_io_handle, &tp_cfg, &touch_handle));

    /* Override Espressif driver defaults for better swipe detection.
     *
     * The driver sets PERIODACTIVE=12 (~60Hz) and PERIODMONITOR=40 (25Hz)
     * with auto-switch to monitor mode after 2 seconds idle. At 25Hz the
     * simplified monitor-mode algorithm misses fast swipes entirely.
     *
     * Fix: disable auto-switch to monitor mode and increase the active
     * scan rate to ~100Hz (PERIODACTIVE=6). */
    esp_lcd_panel_io_tx_param(touch_handle->io, FT5x06_ID_G_CTRL,
                              (uint8_t[]){0x00}, 1);
    esp_lcd_panel_io_tx_param(touch_handle->io, FT5x06_ID_G_PERIODACTIVE,
                              (uint8_t[]){6}, 1);
    esp_lcd_panel_io_tx_param(touch_handle->io, FT5x06_ID_G_PERIODMONITOR,
                              (uint8_t[]){6}, 1);
    ESP_LOGI(TAG, "Overrides applied: CTRL=0 (no auto-monitor), PERIODACTIVE=6 (~100Hz)");

    return touch_handle;
}

#endif /* BOARD_TOUCH_DRIVER == TOUCH_FT6336 */
