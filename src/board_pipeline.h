/**
 * Board-level camera pipeline configuration.
 *
 * Builds a cam_pipeline_config_t pre-populated from board_config.h defines,
 * selecting the correct camera driver (DVP or CSI) and LVGL display driver.
 */
#pragma once

#include "board.h"
#include "board_config.h"

#if BOARD_HAS_CAMERA

#include "esp_cam_pipeline.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Build a cam_pipeline_config_t pre-populated from board_config.h.
 *
 * @param display_parent  LVGL parent object (e.g., lv_screen_active())
 * @param i2c_bus         I2C bus handle for camera SCCB (CSI boards).
 *                        Pass board_i2c_get_handle() when the camera shares
 *                        the main I2C bus, or a separately initialized bus
 *                        for boards where the camera uses a different port.
 *                        Ignored for DVP boards.
 * @return Filled config struct ready for cam_pipeline_create()
 */
cam_pipeline_config_t board_pipeline_default_config(void *display_parent,
                                                    void *i2c_bus);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_HAS_CAMERA */
