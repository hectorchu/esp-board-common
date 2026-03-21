#include "board.h"
#include "board_config.h"

#if BOARD_HAS_RTC

#include "board_rtc.h"
#include "board_i2c.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "board_rtc";

#define PCF85063_DEVICE_ADDR    0x51
#define PCF85063_SECONDS        4

static i2c_master_dev_handle_t dev_handle;

static esp_err_t rtc_reg_read(uint8_t reg_addr, uint8_t *data, size_t len)
{
    esp_err_t ret = ESP_FAIL;
    if (board_i2c_lock(0)) {
        ret = i2c_master_transmit_receive(dev_handle, &reg_addr, 1, data, len, pdMS_TO_TICKS(100));
        board_i2c_unlock();
    }
    return ret;
}

static esp_err_t rtc_reg_write(uint8_t reg_addr, uint8_t *data, size_t len)
{
    esp_err_t ret = ESP_FAIL;
    uint8_t buf[len + 1];
    buf[0] = reg_addr;
    memcpy(buf + 1, data, len);
    if (board_i2c_lock(0)) {
        ret = i2c_master_transmit(dev_handle, buf, len + 1, pdMS_TO_TICKS(100));
        board_i2c_unlock();
    }
    return ret;
}

static uint8_t dec2bcd(uint8_t value) { return ((value / 10) << 4) + (value % 10); }
static uint8_t bcd2dec(uint8_t value) { return (((value & 0xF0) >> 4) * 10) + (value & 0xF); }

void board_rtc_init(i2c_master_bus_handle_t bus_handle)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF85063_DEVICE_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle));

    uint8_t seconds = 0;
    rtc_reg_read(PCF85063_SECONDS, &seconds, 1);
    if (seconds & 0x80)
        ESP_LOGI(TAG, "Oscillator stop detected");
    else
        ESP_LOGI(TAG, "RTC has been kept running");

    struct tm now_tm;
    board_rtc_get_time(&now_tm);

    if (now_tm.tm_year < 125 || now_tm.tm_year > 130) {
        now_tm.tm_year = 2025 - 1900;
        now_tm.tm_mon = 0;
        now_tm.tm_mday = 1;
        now_tm.tm_hour = 12;
        now_tm.tm_min = 0;
        now_tm.tm_sec = 0;
        now_tm.tm_isdst = -1;
        board_rtc_set_time(&now_tm);
    }
}

bool board_rtc_get_time(struct tm *now_tm)
{
    uint8_t time_data[7];
    if (rtc_reg_read(PCF85063_SECONDS, time_data, 7) != ESP_OK) {
        ESP_LOGI(TAG, "Read time error");
        return false;
    }
    now_tm->tm_sec = bcd2dec(time_data[0] & 0x7F);
    now_tm->tm_min = bcd2dec(time_data[1] & 0x7F);
    now_tm->tm_hour = bcd2dec(time_data[2] & 0x3F);
    now_tm->tm_mday = bcd2dec(time_data[3] & 0x3F);
    now_tm->tm_wday = bcd2dec(time_data[4] & 0x7);
    now_tm->tm_mon = bcd2dec(time_data[5] & 0x1F) - 1;
    now_tm->tm_year = bcd2dec(time_data[6]) + 100;
    return true;
}

void board_rtc_set_time(struct tm *now_tm)
{
    uint8_t time_data[7];
    time_data[0] = dec2bcd(now_tm->tm_sec) & 0x7F;
    time_data[1] = dec2bcd(now_tm->tm_min) & 0x7F;
    time_data[2] = dec2bcd(now_tm->tm_hour) & 0x3F;
    time_data[3] = dec2bcd(now_tm->tm_mday) & 0x3F;
    time_data[4] = dec2bcd(now_tm->tm_wday) & 0x7;
    time_data[5] = dec2bcd(now_tm->tm_mon + 1) & 0x1F;
    time_data[6] = dec2bcd((now_tm->tm_year - 100) % 100);
    rtc_reg_write(PCF85063_SECONDS, time_data, 7);
}

#endif /* BOARD_HAS_RTC */
