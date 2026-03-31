#include "board.h"
#include "board_config.h"

#if BOARD_TOUCH_DRIVER == TOUCH_GT911

#include "board_touch_gt911.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

static const char *TAG = "touch_gt911";

#define GT911_INIT_RETRIES      5
#define GT911_RETRY_DELAY_MS    200

/**
 * Perform a hardware reset of the GT911 and select I2C address 0x5D.
 *
 * The GT911 address is latched during reset based on the INT pin state:
 *   INT=LOW  during RST rising edge → address 0x5D
 *   INT=HIGH during RST rising edge → address 0x14
 *
 * After reset, the GT911 firmware needs ~300ms to boot before I2C works.
 */
static void gt911_hw_reset(void)
{
#if BOARD_PIN_TOUCH_RST != GPIO_NUM_NC
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BOARD_PIN_TOUCH_RST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    /* If INT pin is available, drive it low to select address 0x5D */
#if BOARD_PIN_TOUCH_INT != GPIO_NUM_NC
    gpio_config_t int_conf = {
        .pin_bit_mask = 1ULL << BOARD_PIN_TOUCH_INT,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&int_conf);
    gpio_set_level(BOARD_PIN_TOUCH_INT, 0);
#endif

    /* Reset sequence: hold RST low for 10ms, then release */
    gpio_set_level(BOARD_PIN_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(BOARD_PIN_TOUCH_RST, 1);

    /* GT911 firmware boot time: ~300ms after reset */
    vTaskDelay(pdMS_TO_TICKS(400));

#if BOARD_PIN_TOUCH_INT != GPIO_NUM_NC
    /* Release INT pin back to input (used for touch interrupt) */
    int_conf.mode = GPIO_MODE_INPUT;
    gpio_config(&int_conf);
#endif

    ESP_LOGI(TAG, "GT911 hardware reset complete");
#endif /* BOARD_PIN_TOUCH_RST != GPIO_NUM_NC */
}

esp_lcd_touch_handle_t board_touch_gt911_init(i2c_master_bus_handle_t bus,
                                               uint16_t x_max, uint16_t y_max)
{
    /* Perform hardware reset to ensure GT911 is in a known state */
    gt911_hw_reset();

    esp_lcd_panel_io_handle_t touch_io_handle;
    esp_lcd_panel_io_i2c_config_t touch_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    touch_io_config.scl_speed_hz = 400000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus, &touch_io_config, &touch_io_handle));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = x_max,
        .y_max = y_max,
        .rst_gpio_num = GPIO_NUM_NC,   /* We already reset above; don't let driver reset again */
        .int_gpio_num = BOARD_PIN_TOUCH_INT,
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    /* Retry in case GT911 firmware is still booting */
    esp_lcd_touch_handle_t touch_handle = NULL;
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < GT911_INIT_RETRIES; attempt++) {
        err = esp_lcd_touch_new_i2c_gt911(touch_io_handle, &tp_cfg, &touch_handle);
        if (err == ESP_OK) break;
        ESP_LOGW(TAG, "GT911 init attempt %d/%d failed (%s), retrying...",
                 attempt + 1, GT911_INIT_RETRIES, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(GT911_RETRY_DELAY_MS));
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "GT911 touch initialized (x_max=%d, y_max=%d)", x_max, y_max);
    return touch_handle;
}

#endif /* BOARD_TOUCH_DRIVER == TOUCH_GT911 */
