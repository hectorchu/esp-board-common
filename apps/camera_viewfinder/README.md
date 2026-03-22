# Camera Viewfinder

Live camera feed displayed on the board's LCD. Demonstrates camera initialization, RGB565 byte-order handling, and dynamic LVGL render interval for real-time display.

## Building

Requires Docker. The build uses a prebaked ESP-IDF v5.5.1 toolchain image.

```bash
# Build for the default board (waveshare_s3_lcd35b)
make dist

# Build for a different board
make dist BOARD=waveshare_s3_lcd35

# Interactive shell inside the build container
make docker-shell
```

Builds use ccache (persisted at `~/.cache/esp-board-common-ccache`) so subsequent builds are fast.

## Flashing

```bash
cd dist/waveshare_s3_lcd35b
esptool.py --chip esp32s3 -b 460800 write_flash @flash_args
```

Or use `idf.py flash` from within `docker-shell`.

## How it works

The app selects a square camera frame size that fits the display's shortest axis:

| Display | Frame size |
|---------|-----------|
| 320x480 (3.5, 3.5B) | 320x320 |
| 240x320 (LCD2) | 240x240 |

The camera outputs big-endian RGB565. The app byte-swaps to little-endian for LVGL, and the display flush callback swaps back to big-endian for the panel.

`board_set_render_interval_ms(10)` overrides the default 500ms LVGL idle sleep to keep the display responsive to camera frame updates.

## Board support

Any board with `BOARD_HAS_CAMERA=1` in its `board_config.h` is supported. Currently:

- `waveshare_s3_lcd35b` (default)
- `waveshare_s3_lcd35`
