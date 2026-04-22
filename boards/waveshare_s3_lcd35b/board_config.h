/**
 * Board configuration: Waveshare ESP32-S3 Touch LCD 3.5B
 *
 * Display:  AXS15231B QSPI (320x480)
 * Touch:    AXS15231B I2C (integrated in display controller)
 * PMIC:     AXP2101
 * IO:       TCA9554 I2C GPIO expander (display reset)
 */
#pragma once

#include "driver/gpio.h"

#define BOARD_NAME              "Waveshare ESP32-S3 Touch LCD 3.5B"

/* ── Display ── */
#define BOARD_DISPLAY_DRIVER    DISPLAY_QEMU
#define BOARD_LCD_H_RES         320
#define BOARD_LCD_V_RES         480
#define BOARD_PIN_LCD_SCLK      GPIO_NUM_5
#define BOARD_PIN_LCD_DATA0     GPIO_NUM_1
#define BOARD_PIN_LCD_DATA1     GPIO_NUM_2
#define BOARD_PIN_LCD_DATA2     GPIO_NUM_3
#define BOARD_PIN_LCD_DATA3     GPIO_NUM_4
#define BOARD_PIN_LCD_CS        GPIO_NUM_12
#define BOARD_PIN_LCD_RST       GPIO_NUM_NC
#define BOARD_PIN_LCD_BL        GPIO_NUM_6
#define BOARD_SPI_HOST          SPI2_HOST
#define BOARD_LCD_PIXEL_CLOCK   (40 * 1000 * 1000)

/* ── Display quirks ── */
#define BOARD_DISPLAY_QSPI              1   /* Uses QSPI (4 data lines) */
#define BOARD_DISPLAY_QUIRK_RASET_BUG   0   /* CASET/RASET broken over QSPI */
#define BOARD_DISPLAY_DIRECT_MODE       1   /* LVGL direct mode for RASET workaround */

/* ── IO Expander (for display reset) ── */
#define BOARD_HAS_IO_EXPANDER   0
#define BOARD_IO_EXPANDER_ADDR  ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000
#define BOARD_IO_EXPANDER_RST_PIN  IO_EXPANDER_PIN_NUM_1

/* ── Touch ── */
#define BOARD_TOUCH_DRIVER      0

/* ── I2C ── */
#define BOARD_PIN_I2C_SDA       GPIO_NUM_8
#define BOARD_PIN_I2C_SCL       GPIO_NUM_7
#define BOARD_I2C_PORT          0

/* ── PMIC ── */
#define BOARD_HAS_PMIC          0
#define BOARD_PMIC_DRIVER       PMIC_AXP2101

/* ── LVGL port tuning ── */
#define BOARD_LVGL_TASK_PRIORITY    5
#define BOARD_LVGL_TASK_STACK       (1024 * 12)
#define BOARD_LVGL_TASK_AFFINITY    1
#define BOARD_LVGL_MAX_SLEEP_MS     500
#define BOARD_LVGL_TIMER_PERIOD_MS  5

/* ── DMA band size for RASET workaround ── */
#define BOARD_LINES_PER_BAND    80

/* ── Camera (DVP) ── */
#ifndef BOARD_HAS_CAMERA
#define BOARD_HAS_CAMERA            1
#endif
#define BOARD_CAMERA_INTERFACE      CAMERA_DVP
#define BOARD_PIN_CAM_XCLK          GPIO_NUM_38
#define BOARD_PIN_CAM_PCLK          GPIO_NUM_41
#define BOARD_PIN_CAM_VSYNC         GPIO_NUM_17
#define BOARD_PIN_CAM_HREF          GPIO_NUM_18
#define BOARD_PIN_CAM_Y2            GPIO_NUM_45
#define BOARD_PIN_CAM_Y3            GPIO_NUM_47
#define BOARD_PIN_CAM_Y4            GPIO_NUM_48
#define BOARD_PIN_CAM_Y5            GPIO_NUM_46
#define BOARD_PIN_CAM_Y6            GPIO_NUM_42
#define BOARD_PIN_CAM_Y7            GPIO_NUM_40
#define BOARD_PIN_CAM_Y8            GPIO_NUM_39
#define BOARD_PIN_CAM_Y9            GPIO_NUM_21
#define BOARD_PIN_CAM_PWDN          GPIO_NUM_NC
#define BOARD_PIN_CAM_RESET         GPIO_NUM_NC
#define BOARD_CAM_XCLK_FREQ        (20 * 1000 * 1000)
#define BOARD_CAM_LEDC_TIMER       LEDC_TIMER_1
#define BOARD_CAM_LEDC_CHANNEL     LEDC_CHANNEL_1

/* ── SD Card (SDMMC) ── */
#ifndef BOARD_HAS_SDCARD
#define BOARD_HAS_SDCARD            1
#endif
#define BOARD_PIN_SD_CLK            GPIO_NUM_11
#define BOARD_PIN_SD_CMD            GPIO_NUM_10
#define BOARD_PIN_SD_D0             GPIO_NUM_9

/* ── Audio (ES8311) — disabled by default, enable when needed ── */
#ifndef BOARD_HAS_AUDIO
#define BOARD_HAS_AUDIO             0
#endif
#define BOARD_PIN_I2S_MCK           GPIO_NUM_44
#define BOARD_PIN_I2S_BCK           GPIO_NUM_13
#define BOARD_PIN_I2S_LRCK          GPIO_NUM_15
#define BOARD_PIN_I2S_DOUT          GPIO_NUM_16
#define BOARD_PIN_I2S_DIN           GPIO_NUM_14

/* ── RTC (PCF85063) ── */
#ifndef BOARD_HAS_RTC
#define BOARD_HAS_RTC               1
#endif

/* ── IMU (QMI8658) ── */
#ifndef BOARD_HAS_IMU
#define BOARD_HAS_IMU               1
#endif
