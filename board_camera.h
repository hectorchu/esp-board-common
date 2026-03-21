#pragma once

#include "esp_camera.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise camera (DVP interface).
 * Pin mapping and XCLK frequency come from board_config.h.
 */
void board_camera_init(i2c_port_num_t i2c_port);

#ifdef __cplusplus
}
#endif
