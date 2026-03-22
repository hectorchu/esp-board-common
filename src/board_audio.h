#pragma once

#include <stdio.h>
#include <stdint.h>
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise ES8311 audio codec. Pin mapping from board_config.h. */
void board_audio_init(i2c_master_bus_handle_t bus_handle);

/** Record audio into buffer. */
void board_audio_recording(uint8_t *data, size_t limit_size);

/** Play audio from buffer. */
void board_audio_playing(uint8_t *data, size_t limit_size);

#ifdef __cplusplus
}
#endif
