# esp-board-common

Hardware abstraction layer (HAL) for Waveshare ESP32 display boards. Provides a single `board_init()` call that initializes the display, touch, PMIC, and peripherals for any supported board, selected at compile time.

Built as an ESP-IDF component. Apps reference it via `EXTRA_COMPONENT_DIRS` and select a board with `-DBOARD=<name>`.

## Supported boards

| Board | SoC | Display | Touch | Camera |
|-------|-----|---------|-------|--------|
| `waveshare_s3_lcd35b` | ESP32-S3 | AXS15231B 320x480 QSPI | AXS15231B | OV5640 DVP |
| `waveshare_s3_lcd35` | ESP32-S3 | ST7796 320x480 SPI | FT6336 | OV5640 DVP |
| `waveshare_s3_lcd2` | ESP32-S3 | ST7789 240x320 SPI | CST816D | -- |
| `waveshare_p4_lcd35` | ESP32-P4 | ST7796 320x480 SPI | FT6336 | OV5647 CSI |
| `waveshare_p4_lcd43` | ESP32-P4 | ST7701 480x800 MIPI-DSI | GT911 | OV5647 CSI |

## Project structure

```
esp-board-common/
├── src/                    # Component source (drivers, init, LVGL integration)
├── boards/                 # Per-board config (board_config.h, sdkconfig.defaults)
│   ├── waveshare_s3_lcd35b/
│   ├── waveshare_s3_lcd35/
│   ├── waveshare_s3_lcd2/
│   ├── waveshare_p4_lcd35/
│   └── waveshare_p4_lcd43/
├── apps/                   # Standalone demo applications
│   └── camera_viewfinder/
├── CMakeLists.txt          # ESP-IDF component registration
├── idf_component.yml       # Component registry dependencies
└── docs/
```

## Usage

### As an ESP-IDF component

In your app's top-level `CMakeLists.txt`:

```cmake
set(EXTRA_COMPONENT_DIRS "/path/to/esp-board-common")
```

In your app's `main/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "main.c"
    REQUIRES "esp-board-common"
)
```

Build with a board selection:

```
idf.py -DBOARD=waveshare_s3_lcd35b build
```

### Minimal app

```c
#include "board.h"
#include "board_backlight.h"

void app_main(void)
{
    lv_display_t *disp;
    lv_indev_t *touch;

    board_app_config_t cfg = { .landscape = false };
    board_init(&cfg, &disp, &touch);
    board_backlight_set(100);

    // LVGL is ready — create widgets, start tasks, etc.

    board_run();  // never returns
}
```

## API

### `board_init()`

Initializes all hardware in the correct order (I2C, IO expander, display, PMIC, touch, backlight, LVGL) based on `board_config.h`. Returns LVGL display and touch handles.

### `board_set_render_interval_ms(interval_ms)`

Dynamically controls LVGL's render rate. Use a small value (e.g. 10) for real-time camera or animation, or 0 to revert to the default idle behavior. Must hold the LVGL port lock when calling.

### `board_camera_init(i2c_port, frame_size)`

Initializes the DVP camera with the specified frame size. Only available on boards with `BOARD_HAS_CAMERA=1`. Includes an LEDC stale-state workaround for reliable probe across power cycles.

## Building apps

See [apps/camera_viewfinder/](apps/camera_viewfinder/) for a complete example with Docker build, ccache, and per-board dist output.
