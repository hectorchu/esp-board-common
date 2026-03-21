#include "board.h"
#include "board_config.h"

#if BOARD_HAS_IMU

#include "board_imu.h"
#include "board_i2c.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "board_imu";

#define QMI8658_SENSOR_ADDR     0x6B
#define QMI8658_WHO_AM_I        0
#define QMI8658_CTRL1           2
#define QMI8658_CTRL2           3
#define QMI8658_CTRL3           4
#define QMI8658_CTRL7           8
#define QMI8658_STATUS0         46
#define QMI8658_AX_L            53
#define QMI8658_RESET           96

static i2c_master_dev_handle_t dev_handle;

static esp_err_t imu_reg_read(uint8_t reg_addr, uint8_t *data, size_t len)
{
    esp_err_t ret = ESP_FAIL;
    if (board_i2c_lock(0)) {
        ret = i2c_master_transmit_receive(dev_handle, &reg_addr, 1, data, len, pdMS_TO_TICKS(1000));
        board_i2c_unlock();
    }
    return ret;
}

static esp_err_t imu_reg_write(uint8_t reg_addr, uint8_t *data, size_t len)
{
    esp_err_t ret = ESP_FAIL;
    uint8_t buf[len + 1];
    buf[0] = reg_addr;
    memcpy(buf + 1, data, len);
    if (board_i2c_lock(0)) {
        ret = i2c_master_transmit(dev_handle, buf, len + 1, pdMS_TO_TICKS(1000));
        board_i2c_unlock();
    }
    return ret;
}

void board_imu_init(i2c_master_bus_handle_t bus_handle)
{
    uint8_t id = 0;
    ESP_LOGI(TAG, "QMI8658 Initialize");
    vTaskDelay(pdMS_TO_TICKS(100));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = QMI8658_SENSOR_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle));

    ESP_ERROR_CHECK(imu_reg_read(QMI8658_WHO_AM_I, &id, 1));
    if (0x05 != id) {
        ESP_LOGW(TAG, "QMI8658 not found (id=0x%02x)", id);
        return;
    }
    ESP_LOGI(TAG, "Found QMI8658");

    imu_reg_write(QMI8658_RESET, (uint8_t[]){0xb0}, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    imu_reg_write(QMI8658_CTRL1, (uint8_t[]){0x40}, 1);  /* Address auto-increment */
    imu_reg_write(QMI8658_CTRL7, (uint8_t[]){0x03}, 1);  /* Enable acc + gyro */
    imu_reg_write(QMI8658_CTRL2, (uint8_t[]){0x95}, 1);  /* ACC 4g 250Hz */
    imu_reg_write(QMI8658_CTRL3, (uint8_t[]){0xd5}, 1);  /* GYR 512dps 250Hz */
}

bool board_imu_read_data(board_imu_data_t *data)
{
    uint8_t status;
    uint16_t buf[6];

    if (imu_reg_read(QMI8658_STATUS0, &status, 1) != ESP_OK)
        return false;

    if (!(status & 0x03))
        return false;

    if (imu_reg_read(QMI8658_AX_L, (uint8_t *)buf, 12) != ESP_OK)
        return false;

    data->acc_x = buf[0];
    data->acc_y = buf[1];
    data->acc_z = buf[2];
    data->gyr_x = buf[3];
    data->gyr_y = buf[4];
    data->gyr_z = buf[5];

    float mask;
    mask = (float)data->acc_x / sqrtf((float)data->acc_y * data->acc_y + (float)data->acc_z * data->acc_z);
    data->AngleX = atanf(mask) * 57.29578f;
    mask = (float)data->acc_y / sqrtf((float)data->acc_x * data->acc_x + (float)data->acc_z * data->acc_z);
    data->AngleY = atanf(mask) * 57.29578f;
    mask = sqrtf((float)data->acc_x * data->acc_x + (float)data->acc_y * data->acc_y) / (float)data->acc_z;
    data->AngleZ = atanf(mask) * 57.29578f;

    return true;
}

#endif /* BOARD_HAS_IMU */
