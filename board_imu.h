#pragma once

#include <stdio.h>
#include <stdint.h>
#include "driver/i2c_master.h"

typedef struct {
    int16_t acc_x;
    int16_t acc_y;
    int16_t acc_z;
    int16_t gyr_x;
    int16_t gyr_y;
    int16_t gyr_z;
    float AngleX;
    float AngleY;
    float AngleZ;
    float temp;
} board_imu_data_t;

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise QMI8658 IMU via I2C. */
void board_imu_init(i2c_master_bus_handle_t bus_handle);

/** Read accelerometer + gyroscope data. Returns true on success. */
bool board_imu_read_data(board_imu_data_t *data);

#ifdef __cplusplus
}
#endif
