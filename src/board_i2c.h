#pragma once

#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise I2C master bus with the given pins.
 * Creates a recursive mutex for thread-safe access.
 */
i2c_master_bus_handle_t board_i2c_init(int sda_pin, int scl_pin, int port_num);

/** Lock the I2C bus (recursive, blocks until available). */
bool board_i2c_lock(uint32_t timeout_ms);

/** Unlock the I2C bus. */
void board_i2c_unlock(void);

#ifdef __cplusplus
}
#endif
