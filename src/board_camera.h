#pragma once

#include "board.h"
#include "board_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if BOARD_HAS_CAMERA && BOARD_CAMERA_INTERFACE == CAMERA_DVP
#include "esp_camera.h"
#include "driver/i2c_master.h"

/**
 * Initialise camera (DVP interface).
 * Pin mapping and XCLK frequency come from board_config.h.
 *
 * @param i2c_port  I2C port for SCCB (camera control bus)
 * @param frame_size  Frame size enum (e.g. FRAMESIZE_320X320, FRAMESIZE_HVGA)
 */
void board_camera_init(i2c_port_num_t i2c_port, framesize_t frame_size);
#endif

#if BOARD_HAS_CAMERA && BOARD_CAMERA_INTERFACE == CAMERA_CSI
/**
 * Initialise camera (MIPI-CSI interface).
 * Creates a dedicated SCCB I2C bus; pin mapping from board_config.h.
 */
void board_camera_csi_init(void);
#endif

#ifdef __cplusplus
}
#endif
