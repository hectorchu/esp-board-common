#include "board_i2c.h"
#include "esp_log.h"
#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t i2c_mux;
static i2c_master_bus_handle_t s_bus_handle;

bool board_i2c_lock(uint32_t timeout_ms)
{
    assert(i2c_mux && "board_i2c_init must be called first");
    const TickType_t timeout_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(i2c_mux, timeout_ticks) == pdTRUE;
}

void board_i2c_unlock(void)
{
    assert(i2c_mux && "board_i2c_init must be called first");
    xSemaphoreGiveRecursive(i2c_mux);
}

i2c_master_bus_handle_t board_i2c_init(int sda_pin, int scl_pin, int port_num)
{
    i2c_master_bus_handle_t bus_handle;
    i2c_master_bus_config_t cfg = {};
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.i2c_port = (i2c_port_num_t)port_num;
    cfg.scl_io_num = scl_pin;
    cfg.sda_io_num = sda_pin;
    cfg.glitch_ignore_cnt = 7;
    cfg.flags.enable_internal_pullup = 1;

    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &bus_handle));

    i2c_mux = xSemaphoreCreateRecursiveMutex();
    s_bus_handle = bus_handle;
    return bus_handle;
}

i2c_master_bus_handle_t board_i2c_get_handle(void)
{
    return s_bus_handle;
}
