/**
 * Camera pipeline display driver using an LVGL image widget.
 *
 * Pushes RGB565 frames into an lv_image on screen and provides
 * an overlay parent for app widgets (labels, etc.) on top of the feed.
 */
#pragma once

#include "cam_pipeline_display_driver.h"

extern const cam_pipeline_display_driver_t board_pipeline_lvgl_display_driver;
