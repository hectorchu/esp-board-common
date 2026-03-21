#include "board.h"
#include "board_config.h"

#if BOARD_DISPLAY_DRIVER == DISPLAY_ST7796

#include "board_display_st7796.h"

#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7796.h"
#include "esp_log.h"

static const char *TAG = "display_st7796";

/**
 * Vendor-specific init commands for Waveshare ESP32-S3 Touch LCD 3.5 boards.
 * From Waveshare ESP-IDF demo (esp_lcd_st7796.c, vendor_specific_init_default).
 * The generic ST7796 init in the registry component does not work for these panels.
 */
static const st7796_lcd_init_cmd_t waveshare_st7796_init_cmds[] = {
    {0x11, (uint8_t []){0x00}, 0, 120},
    {0x3A, (uint8_t []){0x05}, 1, 0},
    {0xF0, (uint8_t []){0xC3}, 1, 0},
    {0xF0, (uint8_t []){0x96}, 1, 0},
    {0xB4, (uint8_t []){0x01}, 1, 0},
    {0xB7, (uint8_t []){0xC6}, 1, 0},
    {0xC0, (uint8_t []){0x80, 0x45}, 2, 0},
    {0xC1, (uint8_t []){0x13}, 1, 0},
    {0xC2, (uint8_t []){0xA7}, 1, 0},
    {0xC5, (uint8_t []){0x0A}, 1, 0},
    {0xE8, (uint8_t []){0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33}, 8, 0},
    {0xE0, (uint8_t []){0xD0, 0x08, 0x0F, 0x06, 0x06, 0x33, 0x30, 0x33, 0x47, 0x17, 0x13, 0x13, 0x2B, 0x31}, 14, 0},
    {0xE1, (uint8_t []){0xD0, 0x0A, 0x11, 0x0B, 0x09, 0x07, 0x2F, 0x33, 0x47, 0x38, 0x15, 0x16, 0x2C, 0x32}, 14, 0},
    {0xF0, (uint8_t []){0x3C}, 1, 0},
    {0xF0, (uint8_t []){0x69}, 1, 120},
    {0x21, (uint8_t []){0x00}, 0, 0},
    {0x29, (uint8_t []){0x00}, 0, 0},
};

void board_display_st7796_init(esp_lcd_panel_io_handle_t *io_handle,
                                esp_lcd_panel_handle_t *panel_handle,
                                size_t max_transfer_sz)
{
    ESP_LOGI(TAG, "Install ST7796 panel IO");

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

    st7796_vendor_config_t vendor_config = {};
    vendor_config.init_cmds = waveshare_st7796_init_cmds;
    vendor_config.init_cmds_size = sizeof(waveshare_st7796_init_cmds) / sizeof(waveshare_st7796_init_cmds[0]);

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BOARD_PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    panel_config.vendor_config = (void *)&vendor_config;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(*io_handle, &panel_config, panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(*panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(*panel_handle));
#if defined(BOARD_DISPLAY_INVERT_COLOR) && BOARD_DISPLAY_INVERT_COLOR
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(*panel_handle, true));
#endif
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(*panel_handle, true));
}

#endif /* BOARD_DISPLAY_DRIVER == DISPLAY_ST7796 */
