#include "board_backlight.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "backlight";

#define BL_LEDC_TIMER       LEDC_TIMER_0
#define BL_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define BL_LEDC_CHANNEL     LEDC_CHANNEL_0
#define BL_LEDC_DUTY_RES    LEDC_TIMER_10_BIT
#define BL_LEDC_DUTY        (1024)
#define BL_LEDC_FREQUENCY   (5000)

static uint8_t g_brightness = 0;

void board_backlight_init(int bl_pin)
{
    ledc_timer_config_t timer_cfg = {};
    timer_cfg.speed_mode = BL_LEDC_MODE;
    timer_cfg.timer_num = BL_LEDC_TIMER;
    timer_cfg.duty_resolution = BL_LEDC_DUTY_RES;
    timer_cfg.freq_hz = BL_LEDC_FREQUENCY;
    timer_cfg.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t ch_cfg = {};
    ch_cfg.speed_mode = BL_LEDC_MODE;
    ch_cfg.channel = BL_LEDC_CHANNEL;
    ch_cfg.timer_sel = BL_LEDC_TIMER;
    ch_cfg.intr_type = LEDC_INTR_DISABLE;
    ch_cfg.gpio_num = bl_pin;
    ch_cfg.duty = 0;
    ch_cfg.hpoint = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));
}

void board_backlight_set(uint8_t brightness)
{
    if (brightness > 100) {
        brightness = 100;
        ESP_LOGE(TAG, "Brightness value out of range");
    }

    g_brightness = brightness;
    uint32_t duty = (brightness * (BL_LEDC_DUTY - 1)) / 100;

    ESP_ERROR_CHECK(ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL));

    ESP_LOGI(TAG, "LCD brightness set to %d%%", brightness);
}

uint8_t board_backlight_get(void)
{
    return g_brightness;
}
