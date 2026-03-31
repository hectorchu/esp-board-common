# Camera Viewfinder

Live camera feed displayed fullscreen on the board's LCD. Demonstrates the unified camera API, hardware-accelerated scaling (ESP32-P4), and dynamic LVGL render interval for real-time display.

## Building

Requires Docker. The build uses a prebaked ESP-IDF v5.5.1 toolchain image.

```bash
# Build for the default board (waveshare_s3_lcd35b)
make dist

# Build for a different board
make dist BOARD=waveshare_s3_lcd35
make dist BOARD=waveshare_p4_lcd43
```

For ESP32-P4 boards, use the `seedsigner-micropython-builder-base` image which includes the RISC-V toolchain.

Builds use ccache (persisted at `~/.cache/esp-board-common-ccache`) so subsequent builds are fast.

## Flashing

```bash
# ESP32-S3 boards
cd dist/waveshare_s3_lcd35b
esptool.py --chip esp32s3 -b 460800 write_flash @flash_args

# ESP32-P4 boards
cd dist/waveshare_p4_lcd43
esptool.py --chip esp32p4 -b 460800 write_flash @flash_args
```

Or use `idf.py flash` from within `docker-shell`.

## How it works

The app uses the unified `board_camera` API which works identically on DVP (ESP32-S3) and MIPI-CSI (ESP32-P4) cameras. The sensor frame is scaled to fill the display:

### ESP32-S3 (DVP cameras)

- Square frame capture (320x320 or 240x240 depending on display)
- Software byte-swap for DVP big-endian RGB565 output
- Centered on display

### ESP32-P4 (MIPI-CSI camera)

- OV5647 sensor at 800x1280 RAW8 via MIPI-CSI
- ISP pipeline converts RAW8 to RGB565 (configured via Kconfig)
- Frame capture via V4L2 interface (`esp_video` component)
- **PPA hardware-accelerated scaling** from 800x1280 to 480x800 (zero CPU cost)
- Uniform scaling with centered crop fills the display edge-to-edge

`board_set_render_interval_ms(10)` overrides the default 500ms LVGL idle sleep to keep the display responsive to camera frame updates.

## Board support

Any board with `BOARD_HAS_CAMERA=1` in its `board_config.h` is supported:

| Board | Camera | Display | Scaling |
|-------|--------|---------|---------|
| `waveshare_s3_lcd35b` | OV5640 DVP | 320x480 | Square crop |
| `waveshare_s3_lcd35` | OV5640 DVP | 320x480 | Square crop |
| `waveshare_p4_lcd43` | OV5647 CSI | 480x800 | PPA hardware downscale |
