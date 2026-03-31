/**
 * MIPI-DSI display driver for ST7701-based panels (ESP32-P4).
 *
 * Uses the espressif/esp_lcd_st7701 component.  The ST7701 driver:
 *   1. Sends vendor init commands over a DBI (command-mode) channel
 *   2. Creates a DPI (video-mode) panel for continuous frame streaming
 *   3. Returns a panel handle that wraps the DPI panel
 *
 * The returned panel handle supports esp_lcd_dpi_panel_get_frame_buffer()
 * and esp_lcd_dpi_panel_register_event_callbacks(), so it works directly
 * with lvgl_port_add_disp_dsi().
 *
 * Pin assignments from board_config.h (Waveshare ESP32-P4 WiFi6 Touch LCD 4.3).
 */
#include "board.h"
#include "board_config.h"

#if BOARD_DISPLAY_DRIVER == DISPLAY_ST7701

#include "board_display_st7701.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_st7701.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"

static const char *TAG = "display_st7701";

/**
 * DPI panels don't support swap_xy/mirror via the panel API.
 * The esp_lvgl_port library calls these during display setup,
 * so we override them with no-ops to prevent crashes.
 */
static esp_err_t noop_swap_xy(esp_lcd_panel_t *panel, bool swap_axes)
{
    return ESP_OK;
}

static esp_err_t noop_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
{
    return ESP_OK;
}

/**
 * Vendor-specific init commands for the Waveshare ESP32-P4 WiFi6 Touch LCD 4.3.
 * From waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4.3 BSP (vendor_specific_init_default).
 * These configure the ST7701 panel registers (gamma, power, GIP timing) for
 * this specific panel.  Using the wrong init sequence causes the panel to
 * misinterpret the DPI data stream (horizontal lines / moving bar artifacts).
 */
static const st7701_lcd_init_cmd_t waveshare_st7701_init_cmds[] = {
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xEF, (uint8_t[]){0x08}, 1, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, (uint8_t[]){0x63, 0x00}, 2, 0},
    {0xC1, (uint8_t[]){0x0D, 0x02}, 2, 0},
    {0xC2, (uint8_t[]){0x17, 0x08}, 2, 0},
    {0xCC, (uint8_t[]){0x10}, 1, 0},
    {0xB0, (uint8_t[]){0x40, 0xC9, 0x94, 0x0E, 0x10, 0x05, 0x0B, 0x09, 0x08, 0x26, 0x04, 0x52, 0x10, 0x69, 0x6B, 0x69}, 16, 0},
    {0xB1, (uint8_t[]){0x40, 0xD2, 0x98, 0x0C, 0x92, 0x07, 0x09, 0x08, 0x07, 0x25, 0x02, 0x0E, 0x0C, 0x6E, 0x78, 0x55}, 16, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, (uint8_t[]){0x5D}, 1, 0},
    {0xB1, (uint8_t[]){0x4E}, 1, 0},
    {0xB2, (uint8_t[]){0x87}, 1, 0},
    {0xB3, (uint8_t[]){0x80}, 1, 0},
    {0xB5, (uint8_t[]){0x4E}, 1, 0},
    {0xB7, (uint8_t[]){0x85}, 1, 0},
    {0xB8, (uint8_t[]){0x21}, 1, 0},
    {0xB9, (uint8_t[]){0x10, 0x1F}, 2, 0},
    {0xBB, (uint8_t[]){0x03}, 1, 0},
    {0xBC, (uint8_t[]){0x00}, 1, 0},
    {0xC1, (uint8_t[]){0x78}, 1, 0},
    {0xC2, (uint8_t[]){0x78}, 1, 0},
    {0xD0, (uint8_t[]){0x88}, 1, 0},
    {0xE0, (uint8_t[]){0x00, 0x3A, 0x02}, 3, 0},
    {0xE1, (uint8_t[]){0x04, 0xA0, 0x00, 0xA0, 0x05, 0xA0, 0x00, 0xA0, 0x00, 0x40, 0x40}, 11, 0},
    {0xE2, (uint8_t[]){0x30, 0x00, 0x40, 0x40, 0x32, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00}, 13, 0},
    {0xE3, (uint8_t[]){0x00, 0x00, 0x33, 0x33}, 4, 0},
    {0xE4, (uint8_t[]){0x44, 0x44}, 2, 0},
    {0xE5, (uint8_t[]){0x09, 0x2E, 0xA0, 0xA0, 0x0B, 0x30, 0xA0, 0xA0, 0x05, 0x2A, 0xA0, 0xA0, 0x07, 0x2C, 0xA0, 0xA0}, 16, 0},
    {0xE6, (uint8_t[]){0x00, 0x00, 0x33, 0x33}, 4, 0},
    {0xE7, (uint8_t[]){0x44, 0x44}, 2, 0},
    {0xE8, (uint8_t[]){0x08, 0x2D, 0xA0, 0xA0, 0x0A, 0x2F, 0xA0, 0xA0, 0x04, 0x29, 0xA0, 0xA0, 0x06, 0x2B, 0xA0, 0xA0}, 16, 0},
    {0xEB, (uint8_t[]){0x00, 0x00, 0x4E, 0x4E, 0x00, 0x00, 0x00}, 7, 0},
    {0xEC, (uint8_t[]){0x08, 0x01}, 2, 0},
    {0xED, (uint8_t[]){0xB0, 0x2B, 0x98, 0xA4, 0x56, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF7, 0x65, 0x4A, 0x89, 0xB2, 0x0B}, 16, 0},
    {0xEF, (uint8_t[]){0x08, 0x08, 0x08, 0x45, 0x3F, 0x54}, 6, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x29, (uint8_t[]){0x00}, 0, 0},
};

void board_display_st7701_init(esp_lcd_panel_io_handle_t *io_handle,
                               esp_lcd_panel_handle_t *panel_handle)
{
    ESP_LOGI(TAG, "Initialising ST7701 MIPI-DSI display (%dx%d)",
             BOARD_LCD_H_RES, BOARD_LCD_V_RES);

    /* Step 1: Power D-PHY via on-chip LDO */
    esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = BOARD_MIPI_DSI_PHY_LDO_CHAN,
        .voltage_mv = BOARD_MIPI_DSI_PHY_LDO_MV,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &ldo_mipi_phy));
    ESP_LOGI(TAG, "D-PHY LDO channel %d enabled at %d mV",
             BOARD_MIPI_DSI_PHY_LDO_CHAN, BOARD_MIPI_DSI_PHY_LDO_MV);

    /* Step 2: Create MIPI-DSI bus */
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = BOARD_MIPI_DSI_LANE_NUM,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = BOARD_MIPI_DSI_LANE_BITRATE_MBPS,
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));

    /* Step 3: Create DBI (command-mode) IO for ST7701 init sequence */
    esp_lcd_panel_io_handle_t dbi_io = NULL;
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &dbi_io));

    /* Step 4: Configure DPI (video-mode) panel timing */
    esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = BOARD_MIPI_DPI_CLK_MHZ,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = BOARD_MIPI_DPI_NUM_FBS,
        .video_timing = {
            .h_size = BOARD_LCD_H_RES,
            .v_size = BOARD_LCD_V_RES,
            .hsync_back_porch = BOARD_MIPI_DPI_HBP,
            .hsync_pulse_width = BOARD_MIPI_DPI_HSYNC,
            .hsync_front_porch = BOARD_MIPI_DPI_HFP,
            .vsync_back_porch = BOARD_MIPI_DPI_VBP,
            .vsync_pulse_width = BOARD_MIPI_DPI_VSYNC,
            .vsync_front_porch = BOARD_MIPI_DPI_VFP,
        },
        .flags = {
            .use_dma2d = true,
        },
    };

    /* Step 5: Create ST7701 panel (sends init commands via DBI, creates DPI panel) */
    st7701_vendor_config_t vendor_config = {
        .init_cmds = waveshare_st7701_init_cmds,
        .init_cmds_size = sizeof(waveshare_st7701_init_cmds) / sizeof(waveshare_st7701_init_cmds[0]),
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
        },
        .flags = {
            .use_mipi_interface = 1,
        },
    };

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BOARD_PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7701(dbi_io, &panel_config, panel_handle));

    /* Step 6: Reset + init + display on */
    ESP_ERROR_CHECK(esp_lcd_panel_reset(*panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(*panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(*panel_handle, true));

    /* Step 7: Override unsupported DPI panel operations with no-ops.
     * The esp_lvgl_port library calls swap_xy/mirror during display setup
     * which DPI panels don't support.  Without these overrides the
     * library gets an ESP_ERR_NOT_SUPPORTED error and crashes. */
    (*panel_handle)->swap_xy = noop_swap_xy;
    (*panel_handle)->mirror = noop_mirror;

    /* No separate IO handle for LVGL with MIPI-DSI */
    *io_handle = NULL;

    ESP_LOGI(TAG, "ST7701 MIPI-DSI display initialized (%d lanes, %d Mbps, %d MHz DPI)",
             BOARD_MIPI_DSI_LANE_NUM, BOARD_MIPI_DSI_LANE_BITRATE_MBPS,
             BOARD_MIPI_DPI_CLK_MHZ);
}

#endif /* BOARD_DISPLAY_DRIVER == DISPLAY_ST7701 */
