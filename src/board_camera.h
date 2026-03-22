#pragma once

#include "esp_camera.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise camera (DVP interface).
 * Pin mapping and XCLK frequency come from board_config.h.
 *
 * @param i2c_port  I2C port for SCCB (camera control bus)
 * @param frame_size  Frame size enum (e.g. FRAMESIZE_320X320, FRAMESIZE_HVGA)
 */
void board_camera_init(i2c_port_num_t i2c_port, framesize_t frame_size);

#ifdef __cplusplus
}
#endif
