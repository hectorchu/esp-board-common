/**
 * Generic board initialisation.
 *
 * Reads board_config.h (selected at build time via BOARD CMake variable)
 * and dispatches to the correct driver init functions.
 *
 * Supports landscape orientation via board_app_config_t.landscape flag.
 * On RASET-bug boards (AXS15231B), landscape uses a per-pixel 90° CW
 * rotation flush callback; portrait uses memcpy + byte swap.
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "board.h"
#include "board_config.h"

#include "board_i2c.h"
#include "board_backlight.h"

#if BOARD_HAS_PMIC
#include "board_pmic.h"
#endif

#if BOARD_DISPLAY_DRIVER == DISPLAY_AXS15231B
#include "board_display_axs15231b.h"
#include "esp_lcd_axs15231b.h"
#elif BOARD_DISPLAY_DRIVER == DISPLAY_ST7796
#include "board_display_st7796.h"
#elif BOARD_DISPLAY_DRIVER == DISPLAY_ST7789
#include "board_display_st7789.h"
#endif

#if BOARD_TOUCH_DRIVER == TOUCH_AXS15231B
#include "board_touch_axs15231b.h"
#elif BOARD_TOUCH_DRIVER == TOUCH_FT6336
#include "board_touch_ft6336.h"
#elif BOARD_TOUCH_DRIVER == TOUCH_CST816D
#include "board_touch_cst816d.h"
#endif

#if BOARD_HAS_IO_EXPANDER
#include "esp_io_expander_tca9554.h"
#endif

#include "esp_lvgl_port.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"

static const char *TAG = "board";

/* ── Resolution globals ── */
const int LCD_H_RES_VAL = BOARD_LCD_H_RES;
const int LCD_V_RES_VAL = BOARD_LCD_V_RES;

/* ── Hardware handles ── */
static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_touch_handle_t touch_handle = NULL;

#if BOARD_HAS_IO_EXPANDER
static esp_io_expander_handle_t expander_handle = NULL;
#endif

/* ── AXS15231B RASET workaround (custom flush + DMA bounce) ── */
#if BOARD_DISPLAY_QUIRK_RASET_BUG

static SemaphoreHandle_t flush_done_sem = NULL;
static uint8_t *swap_buf[2] = {NULL, NULL};

static bool flush_ready_cb(esp_lcd_panel_io_handle_t panel_io,
                           esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(flush_done_sem, &woken);
    return (woken == pdTRUE);
}

/**
 * Portrait flush callback: memcpy + byte swap, banded DMA.
 * Used when landscape == false on RASET-bug boards.
 */
static void portrait_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    if (!lv_display_flush_is_last(disp)) {
        lv_display_flush_ready(disp);
        return;
    }

    const int bpp = 2;  /* RGB565 */
    int buf_idx = 0;

    for (int y = 0; y < BOARD_LCD_V_RES; y += BOARD_LINES_PER_BAND) {
        int band_h = (y + BOARD_LINES_PER_BAND > BOARD_LCD_V_RES)
                     ? BOARD_LCD_V_RES - y : BOARD_LINES_PER_BAND;
        int band_bytes = BOARD_LCD_H_RES * band_h * bpp;
        uint8_t *src = px_map + (y * BOARD_LCD_H_RES * bpp);
        uint8_t *dst = swap_buf[buf_idx];

        memcpy(dst, src, band_bytes);
        lv_draw_sw_rgb565_swap(dst, BOARD_LCD_H_RES * band_h);

        if (y > 0) {
            xSemaphoreTake(flush_done_sem, portMAX_DELAY);
        }

        esp_lcd_panel_draw_bitmap(panel_handle, 0, y, BOARD_LCD_H_RES, y + band_h, dst);
        buf_idx ^= 1;
    }

    xSemaphoreTake(flush_done_sem, portMAX_DELAY);
    lv_display_flush_ready(disp);
}

/**
 * Landscape flush callback: 90° CW rotation + byte swap, banded DMA.
 * Used when landscape == true on RASET-bug boards.
 *
 * LVGL renders in landscape (V_RES x H_RES) into a SPIRAM framebuffer.
 * This callback rotates pixels 90° CW to portrait and sends 320-wide
 * portrait bands to the panel.
 *
 * Landscape LVGL dimensions: hres=LCD_V_RES (480), vres=LCD_H_RES (320)
 * Panel physical dimensions: 320 wide x 480 tall
 */
static void landscape_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    if (!lv_display_flush_is_last(disp)) {
        lv_display_flush_ready(disp);
        return;
    }

    /* Landscape framebuffer: DISP_H_RES=LCD_V_RES wide, DISP_V_RES=LCD_H_RES tall */
    const int DISP_H_RES = BOARD_LCD_V_RES;  /* 480 */
    const int DISP_V_RES = BOARD_LCD_H_RES;  /* 320 */

    uint16_t *fb = (uint16_t *)px_map;
    int buf_idx = 0;

    for (int py = 0; py < BOARD_LCD_V_RES; py += BOARD_LINES_PER_BAND) {
        int band_h = (py + BOARD_LINES_PER_BAND > BOARD_LCD_V_RES)
                     ? BOARD_LCD_V_RES - py : BOARD_LINES_PER_BAND;
        uint16_t *dst = (uint16_t *)swap_buf[buf_idx];

        /* 90° CW rotation: panel(px, py) ← framebuffer(py, DISP_V_RES-1-px)
         * Loop order: px outer, by inner — sequential fb reads for cache efficiency */
        for (int px = 0; px < BOARD_LCD_H_RES; px++) {
            int fb_y = DISP_V_RES - 1 - px;
            int fb_row_offset = fb_y * DISP_H_RES + py;
            for (int by = 0; by < band_h; by++) {
                uint16_t pixel = fb[fb_row_offset + by];
                dst[by * BOARD_LCD_H_RES + px] = (pixel >> 8) | (pixel << 8);
            }
        }

        if (py > 0) {
            xSemaphoreTake(flush_done_sem, portMAX_DELAY);
        }

        esp_lcd_panel_draw_bitmap(panel_handle, 0, py, BOARD_LCD_H_RES, py + band_h, dst);
        buf_idx ^= 1;
    }

    xSemaphoreTake(flush_done_sem, portMAX_DELAY);
    lv_display_flush_ready(disp);
}

#endif /* BOARD_DISPLAY_QUIRK_RASET_BUG */

/* ── IO Expander ── */
#if BOARD_HAS_IO_EXPANDER
static void io_expander_init(i2c_master_bus_handle_t bus)
{
    ESP_ERROR_CHECK(esp_io_expander_new_i2c_tca9554(bus, BOARD_IO_EXPANDER_ADDR, &expander_handle));
    ESP_ERROR_CHECK(esp_io_expander_set_dir(expander_handle, BOARD_IO_EXPANDER_RST_PIN, IO_EXPANDER_OUTPUT));
    ESP_ERROR_CHECK(esp_io_expander_set_level(expander_handle, BOARD_IO_EXPANDER_RST_PIN, 0));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(esp_io_expander_set_level(expander_handle, BOARD_IO_EXPANDER_RST_PIN, 1));
    vTaskDelay(pdMS_TO_TICKS(200));
}
#endif

/* ── LVGL port setup ── */
static void lvgl_port_setup(const board_app_config_t *app_cfg,
                            lv_display_t **disp_out, lv_indev_t **touch_out)
{
    bool landscape = app_cfg && app_cfg->landscape;

#if BOARD_DISPLAY_QUIRK_RASET_BUG
    flush_done_sem = xSemaphoreCreateBinary();
    swap_buf[0] = heap_caps_malloc(BOARD_LCD_H_RES * BOARD_LINES_PER_BAND * 2, MALLOC_CAP_DMA);
    swap_buf[1] = heap_caps_malloc(BOARD_LCD_H_RES * BOARD_LINES_PER_BAND * 2, MALLOC_CAP_DMA);
    assert(swap_buf[0] != NULL && swap_buf[1] != NULL);
#endif

    lvgl_port_cfg_t port_cfg = {
        .task_priority = BOARD_LVGL_TASK_PRIORITY,
        .task_stack = BOARD_LVGL_TASK_STACK,
        .task_affinity = BOARD_LVGL_TASK_AFFINITY,
        .task_max_sleep_ms = BOARD_LVGL_MAX_SLEEP_MS,
        .timer_period_ms = BOARD_LVGL_TIMER_PERIOD_MS,
    };
    lvgl_port_init(&port_cfg);

    /* Determine LVGL display dimensions based on orientation */
    int lvgl_hres, lvgl_vres;
#if BOARD_DISPLAY_QUIRK_RASET_BUG
    if (landscape) {
        /* Landscape on RASET board: LVGL sees rotated dimensions.
         * Rotation is handled by the custom flush callback. */
        lvgl_hres = BOARD_LCD_V_RES;  /* 480 */
        lvgl_vres = BOARD_LCD_H_RES;  /* 320 */
    } else {
        lvgl_hres = BOARD_LCD_H_RES;  /* 320 */
        lvgl_vres = BOARD_LCD_V_RES;  /* 480 */
    }
#else
    if (landscape) {
        /* Landscape on standard SPI board: LVGL sees swapped dimensions.
         * Panel hardware handles rotation via MADCTL (swap_xy + mirror). */
        lvgl_hres = BOARD_LCD_V_RES;  /* 480 */
        lvgl_vres = BOARD_LCD_H_RES;  /* 320 */
    } else {
        lvgl_hres = BOARD_LCD_H_RES;  /* 320 */
        lvgl_vres = BOARD_LCD_V_RES;  /* 480 */
    }
#endif

    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
#if BOARD_DISPLAY_DIRECT_MODE
        .buffer_size = lvgl_hres * lvgl_vres,
#else
        .buffer_size = BOARD_LCD_H_RES * 40,
#endif
        .trans_size = 0,
        .hres = lvgl_hres,
        .vres = lvgl_vres,
        .color_format = LV_COLOR_FORMAT_RGB565,
#if BOARD_DISPLAY_DIRECT_MODE
        .flags = {
            .buff_spiram = true,
            .direct_mode = true,
        },
#else
        /* Hardware rotation via panel MADCTL — no sw_rotate needed.
         * Values from Waveshare ESP-IDF demo (90° landscape config). */
        .rotation = {
            .swap_xy = landscape ? true : false,
            .mirror_x = landscape ? true : false,
            .mirror_y = landscape ? true : false,
        },
        .flags = {
            .buff_spiram = true,
            .swap_bytes = true,
        },
#endif
    };

    /* CRITICAL: Hold LVGL lock across display add + callback overrides.
     * Without this, the LVGL task can run a frame with the default
     * esp_lvgl_port flush callback, corrupting the panel's write pointer
     * on RASET-bug boards. */
    lvgl_port_lock(0);

    *disp_out = lvgl_port_add_disp(&disp_cfg);

#if BOARD_DISPLAY_QUIRK_RASET_BUG
    /* Override flush callback with RASET workaround */
    lv_display_set_flush_cb(*disp_out,
                            landscape ? landscape_flush_cb : portrait_flush_cb);

    /* Register DMA completion callback */
    const esp_lcd_panel_io_callbacks_t io_cbs = {
        .on_color_trans_done = flush_ready_cb,
    };
    esp_lcd_panel_io_register_event_callbacks(io_handle, &io_cbs, *disp_out);
#endif

    lvgl_port_unlock();

    /* Touch input */
    lvgl_port_touch_cfg_t touch_cfg = {
        .disp = *disp_out,
        .handle = touch_handle,
    };
    *touch_out = lvgl_port_add_touch(&touch_cfg);
}

/* ── Board interface implementation ── */

int board_init(const board_app_config_t *app_cfg,
               lv_display_t **disp, lv_indev_t **touch_indev)
{
    ESP_LOGI(TAG, "Initializing %s...", BOARD_NAME);

    bool landscape = app_cfg && app_cfg->landscape;

    /* Step 1: I2C bus */
    i2c_master_bus_handle_t i2c_bus = board_i2c_init(
        BOARD_PIN_I2C_SDA, BOARD_PIN_I2C_SCL, BOARD_I2C_PORT);

    /* Step 2: IO expander (if present) */
#if BOARD_HAS_IO_EXPANDER
    io_expander_init(i2c_bus);
#endif

    /* Step 3: PMIC (if present) */
#if BOARD_HAS_PMIC
    board_pmic_init(i2c_bus);
#endif

    /* Step 4: Display */
#if BOARD_DISPLAY_DRIVER == DISPLAY_AXS15231B
    board_display_axs15231b_init(&io_handle, &panel_handle,
                                  BOARD_LCD_H_RES * BOARD_LCD_V_RES);
#elif BOARD_DISPLAY_DRIVER == DISPLAY_ST7796
    board_display_st7796_init(&io_handle, &panel_handle,
                               BOARD_LCD_H_RES * BOARD_LCD_V_RES);
#elif BOARD_DISPLAY_DRIVER == DISPLAY_ST7789
    board_display_st7789_init(&io_handle, &panel_handle,
                               BOARD_LCD_H_RES * BOARD_LCD_V_RES);
#endif

    /* Step 5: Touch */
#ifndef BOARD_TOUCH_X_MAX
#define BOARD_TOUCH_X_MAX BOARD_LCD_H_RES
#endif
#ifndef BOARD_TOUCH_Y_MAX
#define BOARD_TOUCH_Y_MAX BOARD_LCD_V_RES
#endif

    /* Touch coordinate max and flags depend on orientation */
    uint16_t touch_x_max = BOARD_TOUCH_X_MAX;
    uint16_t touch_y_max = BOARD_TOUCH_Y_MAX;

#if BOARD_TOUCH_DRIVER == TOUCH_AXS15231B
    if (landscape) {
        /* Touch IC reports in physical portrait (320x480).
         * swap_xy + mirror_x transforms to landscape for LVGL.
         * We init with portrait coords; the driver flags handle the transform. */
        touch_handle = board_touch_axs15231b_init(i2c_bus, touch_x_max, touch_y_max);
        /* Apply landscape transform to the touch handle */
        touch_handle->config.flags.swap_xy = 1;
        touch_handle->config.flags.mirror_x = 1;
    } else {
        touch_handle = board_touch_axs15231b_init(i2c_bus, touch_x_max, touch_y_max);
    }
#elif BOARD_TOUCH_DRIVER == TOUCH_FT6336
    touch_handle = board_touch_ft6336_init(i2c_bus, touch_x_max, touch_y_max);
    if (landscape) {
        /* Values from Waveshare demo 90° rotation config */
        touch_handle->config.flags.swap_xy = 1;
        touch_handle->config.flags.mirror_x = 0;
        touch_handle->config.flags.mirror_y = 1;
    }
#elif BOARD_TOUCH_DRIVER == TOUCH_CST816D
    touch_handle = board_touch_cst816d_init(i2c_bus, touch_x_max, touch_y_max);
#endif

    /* Step 6: Backlight */
    board_backlight_init(BOARD_PIN_LCD_BL);
    board_backlight_set(100);

    /* Step 7: LVGL port */
    lvgl_port_setup(app_cfg, disp, touch_indev);

    ESP_LOGI(TAG, "Board initialized (landscape=%d).", landscape);
    return 0;
}

void board_run(void)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
