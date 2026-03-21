/**
 * Board configuration: Waveshare ESP32-P4 WiFi6 Touch LCD 3.5
 *
 * Display:  ST7796 SPI (320x480)
 * Touch:    FT6336 I2C (FT5x06 family)
 * PMIC:     AXP2101
 * SoC:      ESP32-P4 (RISC-V 400MHz, 32MB PSRAM)
 *
 * NOTE: Pin assignments are preliminary — verify against Waveshare
 * schematic before first hardware test. P4 GPIO numbering differs
 * from S3.
 */
#pragma once

#include "driver/gpio.h"

#define BOARD_NAME              "Waveshare ESP32-P4 WiFi6 Touch LCD 3.5"

/* ── Display ── */
#define BOARD_DISPLAY_DRIVER    DISPLAY_ST7796
#define BOARD_LCD_H_RES         320
#define BOARD_LCD_V_RES         480
#define BOARD_PIN_LCD_SCLK      GPIO_NUM_5
#define BOARD_PIN_LCD_MOSI      GPIO_NUM_1
#define BOARD_PIN_LCD_CS        GPIO_NUM_12
#define BOARD_PIN_LCD_DC        GPIO_NUM_10
#define BOARD_PIN_LCD_RST       GPIO_NUM_NC
#define BOARD_PIN_LCD_BL        GPIO_NUM_6
#define BOARD_SPI_HOST          SPI2_HOST
#define BOARD_LCD_PIXEL_CLOCK   (40 * 1000 * 1000)

/* ── Display quirks ── */
#define BOARD_DISPLAY_QSPI              0
#define BOARD_DISPLAY_QUIRK_RASET_BUG   0
#define BOARD_DISPLAY_DIRECT_MODE       0

/* ── IO Expander ── */
#define BOARD_HAS_IO_EXPANDER   0

/* ── Touch ── */
#define BOARD_TOUCH_DRIVER      TOUCH_FT6336

/* ── I2C ── */
#define BOARD_PIN_I2C_SDA       GPIO_NUM_8
#define BOARD_PIN_I2C_SCL       GPIO_NUM_7
#define BOARD_I2C_PORT          0

/* ── PMIC ── */
#define BOARD_HAS_PMIC          1
#define BOARD_PMIC_DRIVER       PMIC_AXP2101

/* ── LVGL port tuning ── */
#define BOARD_LVGL_TASK_PRIORITY    5
#define BOARD_LVGL_TASK_STACK       (1024 * 12)
#define BOARD_LVGL_TASK_AFFINITY    -1  /* P4 is single-core, no affinity */
#define BOARD_LVGL_MAX_SLEEP_MS     500
#define BOARD_LVGL_TIMER_PERIOD_MS  5

/* ── Peripherals not present ── */
#define BOARD_HAS_CAMERA            0
#define BOARD_HAS_SDCARD            0
#define BOARD_HAS_AUDIO             0
#define BOARD_HAS_RTC               0
#define BOARD_HAS_IMU               0
