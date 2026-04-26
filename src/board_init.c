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
#elif BOARD_DISPLAY_DRIVER == DISPLAY_ST7701
#include "board_display_st7701.h"
#elif BOARD_DISPLAY_DRIVER == DISPLAY_QEMU
#include "esp_lcd_qemu_rgb.h"
#endif

#if BOARD_TOUCH_DRIVER == TOUCH_AXS15231B
#include "board_touch_axs15231b.h"
#elif BOARD_TOUCH_DRIVER == TOUCH_FT6336
#include "board_touch_ft6336.h"
#elif BOARD_TOUCH_DRIVER == TOUCH_CST816D
#include "board_touch_cst816d.h"
#elif BOARD_TOUCH_DRIVER == TOUCH_GT911
#include "board_touch_gt911.h"
#endif

#if BOARD_HAS_IO_EXPANDER
#include "esp_io_expander_tca9554.h"
#endif

#include "esp_lvgl_port.h"
#include "esp_lcd_touch.h"
#if BOARD_DISPLAY_DRIVER == DISPLAY_ST7701
#include "esp_lcd_mipi_dsi.h"
#endif
#include "lvgl.h"

static const char *TAG = "board";

/* ── Resolution globals ── */
const int LCD_H_RES_VAL = BOARD_LCD_H_RES;
const int LCD_V_RES_VAL = BOARD_LCD_V_RES;

/* ── Hardware handles ── */
static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_touch_handle_t touch_handle = NULL;

/* Accessor for diagnostic/testing (bypasses LVGL) */
esp_lcd_panel_handle_t board_get_panel_handle(void) { return panel_handle; }
esp_lcd_touch_handle_t board_get_touch_handle(void) { return touch_handle; }

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

/* ST7701 landscape rotation: not yet implemented on esp_lvgl_port.
 * See docs/lvgl-display-rotation.md for the full analysis and approach.
 * Portrait mode with direct_mode + avoid_tearing works at 15fps. */

static void qemu_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    if (lv_display_flush_is_last(disp)) {
        void *fb;
        esp_lcd_rgb_qemu_get_frame_buffer(panel_handle, &fb);
        memcpy(fb, px_map, BOARD_LCD_H_RES * BOARD_LCD_V_RES * 2);
        esp_lcd_rgb_qemu_refresh(panel_handle);
    }
    lv_display_flush_ready(disp);
}

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

    /* Initialize esp_lvgl_port (creates LVGL internals + handler task) */
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority    = BOARD_LVGL_TASK_PRIORITY;
    port_cfg.task_stack       = BOARD_LVGL_TASK_STACK;
    port_cfg.task_affinity    = BOARD_LVGL_TASK_AFFINITY;
    port_cfg.timer_period_ms  = BOARD_LVGL_TIMER_PERIOD_MS;
    port_cfg.task_max_sleep_ms = BOARD_LVGL_MAX_SLEEP_MS;
    port_cfg.task_stack_caps  = MALLOC_CAP_SPIRAM;
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    /* Determine LVGL display dimensions based on orientation.
     * ST7701 (DSI): portrait uses direct_mode with physical dimensions;
     *   landscape uses swapped dimensions with custom PPA flush callback.
     * RASET / standard SPI: LVGL sees rotated dimensions; the flush
     *   callback or MADCTL handles the physical transformation. */
#if BOARD_DISPLAY_DRIVER != DISPLAY_ST7701
    int lvgl_hres, lvgl_vres;
    if (landscape) {
        lvgl_hres = BOARD_LCD_V_RES;
        lvgl_vres = BOARD_LCD_H_RES;
    } else {
        lvgl_hres = BOARD_LCD_H_RES;
        lvgl_vres = BOARD_LCD_V_RES;
    }
#endif

    /* ── Register display ── */
#if BOARD_DISPLAY_DRIVER == DISPLAY_ST7701
    /* MIPI-DSI: direct mode with DPI hardware framebuffers.
     * avoid_tearing uses the panel's triple-buffered framebuffers.
     * Landscape rotation is not yet supported — see docs/lvgl-display-rotation.md. */
    lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = panel_handle,
        .hres         = BOARD_LCD_H_RES,
        .vres         = BOARD_LCD_V_RES,
        .buffer_size  = BOARD_LCD_H_RES * 50 * sizeof(lv_color16_t),
        .flags = {
            .direct_mode = 1,
        },
    };
    const lvgl_port_display_dsi_cfg_t dsi_cfg = {
        .flags = { .avoid_tearing = 1 },
    };
    *disp_out = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);

#elif BOARD_DISPLAY_DRIVER == DISPLAY_QEMU
    lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = panel_handle,
        .hres         = lvgl_hres,
        .vres         = lvgl_vres,
        .buffer_size  = lvgl_hres * lvgl_vres,
        .flags = {
            .direct_mode = 1,
        },
    };
    lvgl_port_display_rgb_cfg_t rgb_cfg = { 0 };
    *disp_out = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    lv_display_set_flush_cb(*disp_out, qemu_flush_cb);

#elif BOARD_DISPLAY_QUIRK_RASET_BUG
    /* RASET boards: full-frame direct mode with custom flush callback.
     * Don't pass io_handle — we register our own IO callback below. */
    lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle  = panel_handle,
        .hres          = lvgl_hres,
        .vres          = lvgl_vres,
        .buffer_size   = (uint32_t)lvgl_hres * lvgl_vres * sizeof(lv_color16_t),
        .double_buffer = true,
        .flags = {
            .buff_spiram  = 1,
            .direct_mode  = 1,
        },
    };
    *disp_out = lvgl_port_add_disp(&disp_cfg);

#else
    /* Standard SPI boards (ST7796, ST7789): partial updates with PSRAM. */
    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle     = io_handle,
        .panel_handle  = panel_handle,
        .hres          = lvgl_hres,
        .vres          = lvgl_vres,
        .buffer_size   = (uint32_t)lvgl_hres * (lvgl_vres / 2) * sizeof(lv_color16_t),
        .double_buffer = true,
        .flags = {
            .buff_spiram = 1,
            .swap_bytes  = 1,
        },
    };
    *disp_out = lvgl_port_add_disp(&disp_cfg);
#endif
    assert(*disp_out != NULL);

    /* ── Panel MADCTL for standard SPI boards ── */
#if BOARD_DISPLAY_DRIVER != DISPLAY_ST7701 && !BOARD_DISPLAY_QUIRK_RASET_BUG
    if (landscape) {
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, true, true);
    } else {
#if defined(BOARD_DISPLAY_MIRROR_X) && BOARD_DISPLAY_MIRROR_X
        esp_lcd_panel_mirror(panel_handle, true, false);
#endif
    }
#endif

    /* ── Custom flush callback overrides ── */
#if BOARD_DISPLAY_QUIRK_RASET_BUG
    lvgl_port_lock(0);
    lv_display_set_flush_cb(*disp_out,
                            landscape ? landscape_flush_cb : portrait_flush_cb);
    lvgl_port_unlock();
    const esp_lcd_panel_io_callbacks_t io_cbs = {
        .on_color_trans_done = flush_ready_cb,
    };
    esp_lcd_panel_io_register_event_callbacks(io_handle, &io_cbs, *disp_out);
#endif

    /* Touch input (skip if touch init failed) */
    if (touch_handle) {
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp   = *disp_out,
            .handle = touch_handle,
        };
        *touch_out = lvgl_port_add_touch(&touch_cfg);
    } else {
        ESP_LOGW(TAG, "No touch controller — skipping LVGL touch registration");
        if (touch_out) *touch_out = NULL;
    }
}

/* ── Board interface implementation ── */

esp_err_t esp_psram_init(void);
esp_err_t esp_psram_extram_add_to_heap_allocator(void);

int board_init(const board_app_config_t *app_cfg,
               lv_display_t **disp, lv_indev_t **touch_indev)
{
    ESP_LOGI(TAG, "Initializing %s...", BOARD_NAME);

    bool landscape = app_cfg && app_cfg->landscape;

    esp_psram_init();
    esp_psram_extram_add_to_heap_allocator();
    heap_caps_malloc_extmem_enable(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL);

    /* Step 1: I2C bus */
    i2c_master_bus_handle_t i2c_bus = board_i2c_init(
        BOARD_PIN_I2C_SDA, BOARD_PIN_I2C_SCL, BOARD_I2C_PORT);

    /* Step 2: IO expander (if present — resets display hardware) */
#if BOARD_HAS_IO_EXPANDER
    io_expander_init(i2c_bus);
#endif

    /* Step 3: Display — must be initialized BEFORE PMIC.
     * The PMIC init changes voltage rails; doing it between the IO expander
     * reset and the SPI panel init can put the display in a bad state.
     * Matches Waveshare demo order: IO expander → Display → PMIC. */
#if BOARD_DISPLAY_DRIVER == DISPLAY_AXS15231B
    board_display_axs15231b_init(&io_handle, &panel_handle,
                                  BOARD_LCD_H_RES * BOARD_LCD_V_RES * sizeof(lv_color16_t));
#elif BOARD_DISPLAY_DRIVER == DISPLAY_ST7796
    board_display_st7796_init(&io_handle, &panel_handle,
                               BOARD_LCD_H_RES * BOARD_LCD_V_RES * sizeof(lv_color16_t));
#elif BOARD_DISPLAY_DRIVER == DISPLAY_ST7789
    board_display_st7789_init(&io_handle, &panel_handle,
                               BOARD_LCD_H_RES * BOARD_LCD_V_RES * sizeof(lv_color16_t));
#elif BOARD_DISPLAY_DRIVER == DISPLAY_ST7701
    board_display_st7701_init(&io_handle, &panel_handle);
#elif BOARD_DISPLAY_DRIVER == DISPLAY_QEMU
    esp_lcd_rgb_qemu_config_t panel_config = {
        .width  = BOARD_LCD_V_RES,
        .height = BOARD_LCD_H_RES,
        .bpp    = RGB_QEMU_BPP_16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_qemu(&panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
#endif

    /* Step 4: PMIC (if present — after display is fully initialized) */
#if BOARD_HAS_PMIC
    board_pmic_init(i2c_bus);
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
#elif BOARD_TOUCH_DRIVER == TOUCH_GT911
    touch_handle = board_touch_gt911_init(i2c_bus, touch_x_max, touch_y_max);
    if (landscape) {
        touch_handle->config.flags.swap_xy = 1;
        touch_handle->config.flags.mirror_x = 0;
        touch_handle->config.flags.mirror_y = 1;
    }
#endif

    /* Step 6: Backlight — init PWM but keep off (duty=0).
     * Caller turns it on after rendering the first frame to avoid
     * a flash of LVGL's default white background. */
    board_backlight_init(BOARD_PIN_LCD_BL);

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

/* ── Dynamic render interval ── */

static lv_timer_t *render_kick_timer = NULL;

static void render_kick_cb(lv_timer_t *timer)
{
    /* No-op — the timer's existence keeps lv_timer_handler() returning quickly */
    (void)timer;
}

void board_set_render_interval_ms(uint32_t interval_ms)
{
    if (interval_ms > 0) {
        if (render_kick_timer) {
            lv_timer_set_period(render_kick_timer, interval_ms);
        } else {
            render_kick_timer = lv_timer_create(render_kick_cb, interval_ms, NULL);
        }
    } else {
        if (render_kick_timer) {
            lv_timer_delete(render_kick_timer);
            render_kick_timer = NULL;
        }
    }
}
