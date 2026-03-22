#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise backlight PWM on the given GPIO pin. */
void board_backlight_init(int bl_pin);

/** Set backlight brightness (0-100%). */
void board_backlight_set(uint8_t brightness);

/** Get current backlight brightness (0-100%). */
uint8_t board_backlight_get(void);

#ifdef __cplusplus
}
#endif
