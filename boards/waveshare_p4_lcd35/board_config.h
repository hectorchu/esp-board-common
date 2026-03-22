/**
 * Board configuration: Waveshare ESP32-P4 WiFi6 Touch LCD 3.5
 *
 * Display:  ST7796 SPI (320x480)
 * Touch:    FT6336 I2C (FT5x06 family)
 * PMIC:     AXP2101
 * Camera:   OV5647 MIPI-CSI
 * SoC:      ESP32-P4 (dual-core RISC-V 400MHz, 32MB PSRAM)
 *
 * Pin assignments from Waveshare BSP (esp32_p4_wifi6_touch_lcd_35.h).
 */
#pragma once

#include "driver/gpio.h"

#define BOARD_NAME              "Waveshare ESP32-P4 WiFi6 Touch LCD 3.5"

/* ── Display ── */
#define BOARD_DISPLAY_DRIVER    DISPLAY_ST7796
#define BOARD_LCD_H_RES         320
#define BOARD_LCD_V_RES         480
#define BOARD_PIN_LCD_SCLK      GPIO_NUM_21
#define BOARD_PIN_LCD_MOSI      GPIO_NUM_20
#define BOARD_PIN_LCD_CS        GPIO_NUM_23
#define BOARD_PIN_LCD_DC        GPIO_NUM_26
#define BOARD_PIN_LCD_RST       GPIO_NUM_27
#define BOARD_PIN_LCD_BL        GPIO_NUM_28
#define BOARD_SPI_HOST          SPI2_HOST
#define BOARD_LCD_PIXEL_CLOCK   (40 * 1000 * 1000)

/* ── Display quirks ── */
#define BOARD_DISPLAY_QSPI              0
#define BOARD_DISPLAY_QUIRK_RASET_BUG   0
#define BOARD_DISPLAY_DIRECT_MODE       0
#define BOARD_DISPLAY_INVERT_COLOR      0

/* ── IO Expander ── */
#define BOARD_HAS_IO_EXPANDER   0

/* ── Touch ── */
#define BOARD_TOUCH_DRIVER      TOUCH_FT6336
#define BOARD_PIN_TOUCH_RST     GPIO_NUM_29
#define BOARD_PIN_TOUCH_INT     GPIO_NUM_50

/* ── I2C ── */
#define BOARD_PIN_I2C_SDA       GPIO_NUM_7
#define BOARD_PIN_I2C_SCL       GPIO_NUM_8
#define BOARD_I2C_PORT          0

/* ── PMIC ── */
#define BOARD_HAS_PMIC          1
#define BOARD_PMIC_DRIVER       PMIC_AXP2101

/* ── LVGL port tuning ── */
#define BOARD_LVGL_TASK_PRIORITY    5
#define BOARD_LVGL_TASK_STACK       (1024 * 12)
#define BOARD_LVGL_TASK_AFFINITY    -1  /* No core affinity */
#define BOARD_LVGL_MAX_SLEEP_MS     500
#define BOARD_LVGL_TIMER_PERIOD_MS  5

/* ── Camera (MIPI-CSI, OV5647) ── */
#define BOARD_HAS_CAMERA            1
#define BOARD_CAMERA_INTERFACE      CAMERA_CSI
#define BOARD_PIN_CAM_SCCB_SDA      GPIO_NUM_31
#define BOARD_PIN_CAM_SCCB_SCL      GPIO_NUM_34
#define BOARD_CAM_SCCB_I2C_PORT     1   /* Port 0 used by main I2C bus */

/* ── SD Card (4-bit SDMMC) ── */
#define BOARD_HAS_SDCARD            1
#define BOARD_SD_WIDTH              4
#define BOARD_PIN_SD_CLK            GPIO_NUM_43
#define BOARD_PIN_SD_CMD            GPIO_NUM_44
#define BOARD_PIN_SD_D0             GPIO_NUM_39
#define BOARD_PIN_SD_D1             GPIO_NUM_40
#define BOARD_PIN_SD_D2             GPIO_NUM_41
#define BOARD_PIN_SD_D3             GPIO_NUM_42

/* ── Audio (ES8311) ── */
#define BOARD_HAS_AUDIO             1
#define BOARD_PIN_I2S_MCK           GPIO_NUM_13
#define BOARD_PIN_I2S_BCK           GPIO_NUM_12
#define BOARD_PIN_I2S_LRCK          GPIO_NUM_10
#define BOARD_PIN_I2S_DOUT          GPIO_NUM_9
#define BOARD_PIN_I2S_DIN           GPIO_NUM_11
#define BOARD_PIN_PA                GPIO_NUM_53

/* ── RTC / IMU ── */
#define BOARD_HAS_RTC               0
#define BOARD_HAS_IMU               0
