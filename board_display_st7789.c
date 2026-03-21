#include "board.h"
#include "board_config.h"

#if BOARD_DISPLAY_DRIVER == DISPLAY_ST7789

#include "board_display_st7789.h"

#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

static const char *TAG = "display_st7789";

void board_display_st7789_init(esp_lcd_panel_io_handle_t *io_handle,
                                esp_lcd_panel_handle_t *panel_handle,
                                size_t max_transfer_sz)
{
    ESP_LOGI(TAG, "Install ST7789 panel IO");

    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num = BOARD_PIN_LCD_SCLK;
    buscfg.mosi_io_num = BOARD_PIN_LCD_MOSI;
    buscfg.miso_io_num = -1;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = max_transfer_sz;

    ESP_ERROR_CHECK(spi_bus_initialize(BOARD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.dc_gpio_num = BOARD_PIN_LCD_DC;
    io_config.cs_gpio_num = BOARD_PIN_LCD_CS;
    io_config.pclk_hz = BOARD_LCD_PIXEL_CLOCK;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.spi_mode = 0;
    io_config.trans_queue_depth = 10;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_SPI_HOST, &io_config, io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BOARD_PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    /* ST7789 is built into ESP-IDF — no registry component needed */
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(*io_handle, &panel_config, panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(*panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(*panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(*panel_handle, false, false));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(*panel_handle, false));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(*panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(*panel_handle, true));
}

#endif /* BOARD_DISPLAY_DRIVER == DISPLAY_ST7789 */
