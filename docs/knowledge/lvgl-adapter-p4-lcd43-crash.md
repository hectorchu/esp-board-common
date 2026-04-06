# LVGL Adapter Migration — P4 LCD 4.3 Boot Crash

## Summary

The `feature/lvgl-adapter-migration` branch (`b73fb77`) crashes on boot when used with MicroPython on the Waveshare ESP32-P4 WiFi6 Touch LCD 4.3. The board works correctly on `main` (`490f027`) with `esp_lvgl_port`.

The QR decoder demo app on this branch was validated on the P4 LCD 4.3, but that app does not register touch input. The crash manifests when `board_init()` runs the full init sequence including touch.

## Environment

- Board: Waveshare ESP32-P4 WiFi6 Touch LCD 4.3
- Display: ST7701 MIPI-DSI (480x800)
- Touch: GT911 I2C (GPIO7/GPIO8, port 0)
- Camera: OV5647 MIPI-CSI (shares I2C bus)
- Build: MicroPython v1.27.0 via seedsigner-micropython-builder
- Camera module attached during all tests

## Crash 1: With camera sdkconfig additions

The first build included camera Kconfig settings (`CONFIG_CAMERA_OV5647=y`, `CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=y`) and `CONFIG_ESP_TASK_WDT_EN=n`.

### Symptoms

GT911 touch init fails on all 5 retry attempts with I2C NACK errors, then the board panics:

```
E (1282) GT911: touch_gt911_read_cfg(419): GT911 read error!
E (1287) GT911: esp_lcd_touch_new_i2c_gt911(169): GT911 init failed
E (1293) GT911: Error (0x103)! Touch controller GT911 initialization failed!
[... 5 retries, same errors ...]

Guru Meditation Error: Core  0 panic'ed (Load access fault). Exception was unhandled.
MEPC    : 0x4809997e
MTVAL   : 0x00000044
```

`MTVAL: 0x00000044` is a NULL pointer + offset — `board_init.c` line 512-513 dereferences `touch_handle->config.flags` after `board_touch_gt911_init()` returns NULL. The `ESP_ERROR_CHECK(err)` in `board_touch_gt911.c:104` triggers abort, and the panic handler itself faults (double-fault).

### WDT note

`CONFIG_ESP_TASK_WDT_EN=n` causes a linker error with MicroPython because `machine_wdt.c` references `esp_task_wdt_*` symbols. Changed to `CONFIG_ESP_TASK_WDT_TIMEOUT_S=30` which links successfully.

## Crash 2: Without camera sdkconfig (isolation build)

To isolate, the camera Kconfig settings were commented out — only the adapter migration remained as the delta from the working `main` branch.

### Symptoms

No GT911 errors at all. The board crashes immediately during LVGL rendering setup:

```
Guru Meditation Error: Core  0 panic'ed (Load access fault). Exception was unhandled.
MEPC    : 0x480987ba
MTVAL   : 0x4ffc063e
```

Register values suggest an LVGL display rendering context — `S4=0x1c1` (449), `S7=0x1c2` (450) are near the display width (480), and `A2/A6=0x4ffc063e` is a PSRAM address. This crash happens before the REPL starts, during `board_init()` → `lvgl_adapter_setup()`.

## Analysis

Two distinct crash modes, but both trace to the `esp_lvgl_adapter` migration:

1. **Crash 1** (with camera config): The additional `esp_video`/`esp_cam_sensor` components pulled in by `CONFIG_CAMERA_OV5647=y` may be interfering with the I2C bus at startup. The GT911 hardware reset and retry logic is identical between branches — the I2C bus itself is corrupted. Additionally, `board_init.c` has a NULL dereference bug: the GT911 `touch_handle` is used without a NULL check after `board_touch_gt911_init()` fails.

2. **Crash 2** (without camera config): Pure adapter migration issue. The `esp_lv_adapter_init()` / `esp_lv_adapter_register_display()` / `esp_lv_adapter_start()` sequence crashes during initial LVGL rendering on the MIPI-DSI ST7701 panel. This was not caught by the QR decoder app because that app doesn't exercise the same LVGL task startup path in a MicroPython context.

## What changed between working and broken

Only one commit: `b73fb77` (feat: migrate from esp_lvgl_port to esp_lvgl_adapter)

Key changes in `board_init.c`:
- `lvgl_port_init()` → `esp_lv_adapter_init()`
- `lvgl_port_add_disp()` → `esp_lv_adapter_register_display()`
- `lvgl_port_add_touch()` → `esp_lv_adapter_register_touch()`
- New: `esp_lv_adapter_start()` — starts the LVGL handler task after all registration
- Display config struct changed from `lvgl_port_display_cfg_t` to `esp_lv_adapter_display_config_t` with different buffer/profile configuration

The `board_i2c.c`, `board_touch_gt911.c`, and `boards/waveshare_p4_lcd43/` are unchanged between the two commits.

## Bugs to fix in board_common

1. **NULL touch_handle dereference** (`board_init.c:511-516`): After `board_touch_gt911_init()` returns NULL, the code dereferences `touch_handle->config.flags`. Need a NULL guard.

2. **NULL touch_handle passed to adapter** (`board_init.c:429`): `esp_lv_adapter_register_touch()` is called even when `touch_handle` is NULL. Need to skip touch registration when init failed.

3. **LVGL adapter display config for MIPI-DSI** (`board_init.c`): The `esp_lv_adapter_display_config_t` for ST7701 may have incorrect buffer sizing, profile settings, or tear-avoidance mode for this panel. The crash at `MTVAL: 0x4ffc063e` (PSRAM address) during rendering suggests a buffer overrun or misconfigured DMA.

## Reproduction

```bash
# In seedsigner-micropython-builder:
# Pin board_common to b73fb77
cd ports/esp32/board_common && git checkout b73fb77

# Build
BOARD=WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_43 make docker-build-all

# Flash
cd build/WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_43/flash
python -m esptool --chip esp32p4 write_flash @flash_args

# Observe crash on serial console (115200 baud)
```

## Working reference

The same board works with `main` branch (`490f027`) using `esp_lvgl_port`. The 2048 game with touch input works correctly on that build.
