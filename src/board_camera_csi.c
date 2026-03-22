/**
 * MIPI-CSI camera driver for ESP32-P4.
 *
 * Uses the IDF esp_driver_cam CSI controller and the espressif/esp_cam_sensor
 * component for OV5647 sensor control.  The sensor is controlled over a
 * dedicated SCCB I2C bus (separate from the main touch/PMIC I2C bus).
 *
 * NOTE: This driver is pre-hardware and will need tuning once the board
 * arrives.  Key areas that need verification:
 *   - OV5647 sensor detection and format negotiation
 *   - CSI lane bit rate and data lane configuration
 *   - ISP (Image Signal Processor) pipeline settings
 *   - Frame buffer allocation and capture flow
 */
#include "board.h"
#include "board_config.h"

#if BOARD_HAS_CAMERA && BOARD_CAMERA_INTERFACE == CAMERA_CSI

#include "board_camera.h"

#include "driver/i2c_master.h"
#include "esp_sccb_i2c.h"
#include "esp_cam_ctlr_csi.h"
#include "esp_cam_ctlr.h"
#include "esp_cam_sensor.h"
#include "esp_log.h"

/* OV5647 sensor header is not in the component's public include path.
 * Forward-declare the detect function (signature from ov5647.h). */
#define OV5647_SCCB_ADDR 0x36
extern esp_cam_sensor_device_t *ov5647_detect(esp_cam_sensor_config_t *config);

static const char *TAG = "board_camera_csi";

static i2c_master_bus_handle_t sccb_bus = NULL;
static esp_sccb_io_handle_t sccb_io = NULL;
static esp_cam_ctlr_handle_t cam_ctlr = NULL;

/**
 * Initialise SCCB (I2C) bus and create IO handle for OV5647 sensor.
 * Uses a dedicated I2C port separate from the main board I2C bus.
 */
static esp_err_t sccb_init(void)
{
    i2c_master_bus_config_t bus_conf = {
        .i2c_port = BOARD_CAM_SCCB_I2C_PORT,
        .sda_io_num = BOARD_PIN_CAM_SCCB_SDA,
        .scl_io_num = BOARD_PIN_CAM_SCCB_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_conf, &sccb_bus);
    if (err != ESP_OK) return err;

    sccb_i2c_config_t sccb_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OV5647_SCCB_ADDR,
        .scl_speed_hz = 100000,
    };
    return sccb_new_i2c_io(sccb_bus, &sccb_cfg, &sccb_io);
}

void board_camera_csi_init(void)
{
    /* Step 1: SCCB I2C for sensor control */
    esp_err_t err = sccb_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SCCB init failed: %s", esp_err_to_name(err));
        return;
    }

    /* Step 2: Detect OV5647 sensor */
    esp_cam_sensor_config_t sensor_cfg = {
        .sccb_handle = sccb_io,
        .reset_pin = -1,
        .pwdn_pin = -1,
        .xclk_pin = -1,
        .sensor_port = ESP_CAM_SENSOR_MIPI_CSI,
    };

    esp_cam_sensor_device_t *sensor = ov5647_detect(&sensor_cfg);
    if (!sensor) {
        ESP_LOGE(TAG, "OV5647 not detected on SCCB bus (SDA=%d, SCL=%d)",
                 BOARD_PIN_CAM_SCCB_SDA, BOARD_PIN_CAM_SCCB_SCL);
        return;
    }
    ESP_LOGI(TAG, "OV5647 sensor detected");

    /* Step 3: Query and set capture format.
     * Pick the first MIPI-CSI format available from the sensor. */
    esp_cam_sensor_format_array_t fmt_array = {};
    esp_cam_sensor_query_format(sensor, &fmt_array);

    const esp_cam_sensor_format_t *target_fmt = NULL;
    for (int i = 0; i < fmt_array.count; i++) {
        if (fmt_array.format_array[i].port == ESP_CAM_SENSOR_MIPI_CSI) {
            target_fmt = &fmt_array.format_array[i];
            break;
        }
    }
    if (!target_fmt && fmt_array.count > 0) {
        target_fmt = &fmt_array.format_array[0];
    }

    if (target_fmt) {
        err = esp_cam_sensor_set_format(sensor, target_fmt);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set sensor format: %s", esp_err_to_name(err));
            return;
        }
        ESP_LOGI(TAG, "Sensor format: %s (%dx%d @ %dfps)",
                 target_fmt->name ? target_fmt->name : "?",
                 target_fmt->width, target_fmt->height, target_fmt->fps);
    }

    /* Step 4: Configure CSI controller.
     * Use MIPI info from the negotiated sensor format for lane bit rate. */
    int lane_mbps = 200;  /* fallback */
    if (target_fmt && target_fmt->mipi_info.mipi_clk > 0) {
        lane_mbps = target_fmt->mipi_info.mipi_clk / 1000000;
    }

    esp_cam_ctlr_csi_config_t csi_cfg = {
        .ctlr_id = 0,
        .h_res = target_fmt ? target_fmt->width : 480,
        .v_res = target_fmt ? target_fmt->height : 320,
        .data_lane_num = 2,
        .lane_bit_rate_mbps = lane_mbps,
        .input_data_color_type = CAM_CTLR_COLOR_RAW8,
        .output_data_color_type = CAM_CTLR_COLOR_RGB565,
    };

    err = esp_cam_new_csi_ctlr(&csi_cfg, &cam_ctlr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CSI controller init failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "CSI camera initialized (%dx%d, %d Mbps/lane)",
             csi_cfg.h_res, csi_cfg.v_res, lane_mbps);

    /* NOTE: Frame buffer allocation and capture start are deferred until
     * the MicroPython bindings request a frame. The controller is created
     * but not enabled/started here. Call esp_cam_ctlr_enable() and
     * esp_cam_ctlr_start() after registering event callbacks and
     * allocating frame buffers with esp_cam_ctlr_alloc_buffer(). */
}

#endif /* BOARD_HAS_CAMERA && CAMERA_CSI */
