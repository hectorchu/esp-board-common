/**
 * Board abstraction interface.
 *
 * Each board is defined by a board_config.h selected at compile time via the
 * BOARD CMake variable.  The generic board_init() in board_init.c dispatches
 * on the config defines to initialise the correct drivers.
 */
#pragma once

#include "esp_lcd_types.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"

/* ── Driver selection enums ── */
#define DISPLAY_ST7796      1
#define DISPLAY_ST7789      2
#define DISPLAY_AXS15231B   3
#define DISPLAY_ST7701      4
#define DISPLAY_QEMU        5

#define TOUCH_FT6336        1
#define TOUCH_CST816D       2
#define TOUCH_AXS15231B     3
#define TOUCH_GT911         4

#define PMIC_AXP2101        1

#define CAMERA_DVP          1
#define CAMERA_CSI          2

/* ── Resolution (set in board_init.c from board_config.h) ── */
#ifdef __cplusplus
extern "C" {
#endif

extern const int LCD_H_RES_VAL;
extern const int LCD_V_RES_VAL;

/* ── Compile-time orientation (from Kconfig CONFIG_BOARD_LANDSCAPE) ── */
#ifdef CONFIG_BOARD_LANDSCAPE
#define BOARD_LANDSCAPE  1
#else
#define BOARD_LANDSCAPE  0
#endif

/* Logical display dimensions — what LVGL and application code should use.
 * In landscape mode these are the physical dimensions swapped. */
#if BOARD_LANDSCAPE
#define BOARD_DISP_H_RES  BOARD_LCD_V_RES
#define BOARD_DISP_V_RES  BOARD_LCD_H_RES
#else
#define BOARD_DISP_H_RES  BOARD_LCD_H_RES
#define BOARD_DISP_V_RES  BOARD_LCD_V_RES
#endif

/* ── Application-level config (not hardware — passed by the consuming project) ── */
typedef struct {
    bool landscape;     /* true = 90° CW rotation in flush + touch transform */
} board_app_config_t;

/* ── Board interface ── */

/**
 * Initialise all board hardware and the LVGL display/touch port.
 * Returns 0 on success.
 *
 * @param app_cfg   Application config (orientation, etc.). NULL = defaults (portrait).
 * @param disp      Output: LVGL display handle
 * @param touch_indev Output: LVGL touch input device handle
 */
int board_init(const board_app_config_t *app_cfg,
               lv_display_t **disp, lv_indev_t **touch_indev);

/**
 * Board main loop (never returns).
 * ESP32: idle loop with vTaskDelay.
 * Desktop: SDL event pump.
 */
void board_run(void);

/**
 * Set the maximum LVGL render interval.
 *
 * Creates an internal LVGL timer that forces lv_timer_handler() to
 * return within the specified interval, overriding the default
 * BOARD_LVGL_MAX_SLEEP_MS.
 *
 * Call with a small value (e.g. 10) for real-time camera/animation,
 * or 0 to revert to the default idle behavior.
 *
 * Must hold the LVGL port lock when calling.
 *
 * @param interval_ms  Desired render interval in ms, or 0 to disable.
 */
void board_set_render_interval_ms(uint32_t interval_ms);

/** Get LCD panel handle (for diagnostic/testing that bypasses LVGL). */
esp_lcd_panel_handle_t board_get_panel_handle(void);

/** Get touch handle (for direct driver access — e.g. reading strength data
 *  that LVGL discards). Returns NULL if touch init failed. */
esp_lcd_touch_handle_t board_get_touch_handle(void);

#ifdef __cplusplus
}
#endif

/* No app_main(), no game_main() — each project defines its own entry point */
