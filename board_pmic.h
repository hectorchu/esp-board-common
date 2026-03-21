#pragma once

#include <stdio.h>
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise AXP2101 PMIC via I2C. */
esp_err_t board_pmic_init(i2c_master_bus_handle_t bus_handle);

/** Handle PMU interrupt status. */
void board_pmic_isr_handler(void);

#ifdef __cplusplus
}
#endif
