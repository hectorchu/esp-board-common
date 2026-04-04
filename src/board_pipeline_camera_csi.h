/**
 * Camera pipeline CSI driver for ESP32-P4.
 *
 * Wraps the esp_video V4L2 abstraction to implement the pipeline's
 * camera driver interface. Spawns a capture task that loops on
 * VIDIOC_DQBUF and feeds frames to the pipeline via callback.
 */
#pragma once

#include "board.h"
#include "board_config.h"

#if BOARD_HAS_CAMERA && BOARD_CAMERA_INTERFACE == CAMERA_CSI

#include "cam_pipeline_camera_driver.h"
#include "driver/i2c_master.h"

typedef struct {
    i2c_master_bus_handle_t i2c_bus; /* For SCCB sensor communication */
    uint8_t ae_target;              /* Initial AE target (2-235, 0 = use driver default) */
} board_pipeline_csi_config_t;

extern const cam_pipeline_camera_driver_t board_pipeline_csi_driver;

#endif /* BOARD_HAS_CAMERA && CAMERA_CSI */
