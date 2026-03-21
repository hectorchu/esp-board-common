#include "board.h"
#include "board_config.h"

#if BOARD_HAS_CAMERA

#include "board_camera.h"
#include "esp_camera.h"
#include "esp_log.h"

static const char *TAG = "board_camera";

void board_camera_init(i2c_port_num_t i2c_port)
{
    camera_config_t config;
    config.ledc_channel = BOARD_CAM_LEDC_CHANNEL;
    config.ledc_timer = BOARD_CAM_LEDC_TIMER;
    config.pin_d0 = BOARD_PIN_CAM_Y2;
    config.pin_d1 = BOARD_PIN_CAM_Y3;
    config.pin_d2 = BOARD_PIN_CAM_Y4;
    config.pin_d3 = BOARD_PIN_CAM_Y5;
    config.pin_d4 = BOARD_PIN_CAM_Y6;
    config.pin_d5 = BOARD_PIN_CAM_Y7;
    config.pin_d6 = BOARD_PIN_CAM_Y8;
    config.pin_d7 = BOARD_PIN_CAM_Y9;
    config.pin_xclk = BOARD_PIN_CAM_XCLK;
    config.pin_pclk = BOARD_PIN_CAM_PCLK;
    config.pin_vsync = BOARD_PIN_CAM_VSYNC;
    config.pin_href = BOARD_PIN_CAM_HREF;
    config.pin_sccb_sda = -1;
    config.pin_sccb_scl = -1;
    config.sccb_i2c_port = i2c_port;
    config.pin_pwdn = BOARD_PIN_CAM_PWDN;
    config.pin_reset = BOARD_PIN_CAM_RESET;
    config.xclk_freq_hz = BOARD_CAM_XCLK_FREQ;
    config.frame_size = FRAMESIZE_320X480;
    config.pixel_format = PIXFORMAT_RGB565;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.jpeg_quality = 12;
    config.fb_count = 1;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return;
    }

    sensor_t *s = esp_camera_sensor_get();
    s->set_vflip(s, 1);
}

#endif /* BOARD_HAS_CAMERA */
