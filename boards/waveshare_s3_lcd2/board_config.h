/**
 * Board configuration: Waveshare ESP32-S3 Touch LCD 2
 *
 * Display:  ST7789T3 SPI (240x320)
 * Touch:    CST816D I2C (CST816S family)
 * PMIC:     none
 *
 * Pin assignments verified against Waveshare ESP-IDF demo code
 * (ESP32-S3-Touch-LCD-2-Demo.zip, 02_lvgl_qmi8658 example).
 */
#pragma once

#include "driver/gpio.h"

#define BOARD_NAME              "Waveshare ESP32-S3 Touch LCD 2"

/* ── Display ── */
#define BOARD_DISPLAY_DRIVER    DISPLAY_ST7789
#define BOARD_LCD_H_RES         240
#define BOARD_LCD_V_RES         320
#define BOARD_PIN_LCD_SCLK      GPIO_NUM_39
#define BOARD_PIN_LCD_MOSI      GPIO_NUM_38
#define BOARD_PIN_LCD_CS        GPIO_NUM_45
#define BOARD_PIN_LCD_DC        GPIO_NUM_42
#define BOARD_PIN_LCD_RST       GPIO_NUM_NC
#define BOARD_PIN_LCD_BL        GPIO_NUM_1
#define BOARD_SPI_HOST          SPI2_HOST
#define BOARD_LCD_PIXEL_CLOCK   (80 * 1000 * 1000)

/* ── Display quirks ── */
#define BOARD_DISPLAY_QSPI              0
#define BOARD_DISPLAY_QUIRK_RASET_BUG   0
#define BOARD_DISPLAY_DIRECT_MODE       0

/* ── IO Expander ── */
#define BOARD_HAS_IO_EXPANDER   0

/* ── Touch ── */
#define BOARD_TOUCH_DRIVER      TOUCH_CST816D
#define BOARD_TOUCH_SWAP_XY     0
#define BOARD_TOUCH_MIRROR_X    0
#define BOARD_TOUCH_MIRROR_Y    0

/* ── I2C ── */
#define BOARD_PIN_I2C_SDA       GPIO_NUM_48
#define BOARD_PIN_I2C_SCL       GPIO_NUM_47
#define BOARD_I2C_PORT          0

/* ── PMIC ── */
#define BOARD_HAS_PMIC          0

/* ── LVGL port tuning ── */
#define BOARD_LVGL_TASK_PRIORITY    5
#define BOARD_LVGL_TASK_STACK       (1024 * 12)
#define BOARD_LVGL_TASK_AFFINITY    1
#define BOARD_LVGL_MAX_SLEEP_MS     500
#define BOARD_LVGL_TIMER_PERIOD_MS  5

/* ── Camera (DVP) ── */
#ifndef BOARD_HAS_CAMERA
#define BOARD_HAS_CAMERA            1
#endif
#define BOARD_CAMERA_INTERFACE      CAMERA_DVP
#define BOARD_PIN_CAM_XCLK          GPIO_NUM_8
#define BOARD_PIN_CAM_PCLK          GPIO_NUM_9
#define BOARD_PIN_CAM_VSYNC         GPIO_NUM_6
#define BOARD_PIN_CAM_HREF          GPIO_NUM_4
#define BOARD_PIN_CAM_SIOD          GPIO_NUM_21
#define BOARD_PIN_CAM_SIOC          GPIO_NUM_16
#define BOARD_PIN_CAM_Y2            GPIO_NUM_12
#define BOARD_PIN_CAM_Y3            GPIO_NUM_13
#define BOARD_PIN_CAM_Y4            GPIO_NUM_15
#define BOARD_PIN_CAM_Y5            GPIO_NUM_11
#define BOARD_PIN_CAM_Y6            GPIO_NUM_14
#define BOARD_PIN_CAM_Y7            GPIO_NUM_10
#define BOARD_PIN_CAM_Y8            GPIO_NUM_7
#define BOARD_PIN_CAM_Y9            GPIO_NUM_2
#define BOARD_PIN_CAM_PWDN          GPIO_NUM_17
#define BOARD_PIN_CAM_RESET         GPIO_NUM_NC
#define BOARD_CAM_XCLK_FREQ        (20 * 1000 * 1000)
#define BOARD_CAM_LEDC_TIMER       LEDC_TIMER_1
#define BOARD_CAM_LEDC_CHANNEL     LEDC_CHANNEL_1

/* ── Peripherals not present ── */
#ifndef BOARD_HAS_SDCARD
#define BOARD_HAS_SDCARD            0
#endif
#ifndef BOARD_HAS_AUDIO
#define BOARD_HAS_AUDIO             0
#endif
#ifndef BOARD_HAS_RTC
#define BOARD_HAS_RTC               0
#endif
#ifndef BOARD_HAS_IMU
#define BOARD_HAS_IMU               0
#endif
