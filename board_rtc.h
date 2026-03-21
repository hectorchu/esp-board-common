#pragma once

#include <stdio.h>
#include <time.h>
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise PCF85063 RTC via I2C. */
void board_rtc_init(i2c_master_bus_handle_t bus_handle);

/** Read current time from RTC. Returns true on success. */
bool board_rtc_get_time(struct tm *now_tm);

/** Set RTC time. */
void board_rtc_set_time(struct tm *now_tm);

#ifdef __cplusplus
}
#endif
