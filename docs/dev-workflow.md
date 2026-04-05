# ESP Board Common — Development Workflow

Build, flash, monitor, and capture screenshots for ESP-IDF apps via Docker.

## Prerequisites

- Docker image: `ghcr.io/kdmukai-bot/seedsigner-micropython-builder-base:latest`
- Board connected via USB (typically `/dev/ttyACM0`)
- Webcam at `/dev/video0` (for screenshots)

## Build

Apps are in `apps/` (e.g., `qr_decoder`, `camera_viewfinder`). Each has a Makefile.

```bash
cd apps/qr_decoder
make docker-build BOARD=waveshare_p4_lcd35
```

Available boards: `waveshare_p4_lcd35`, `waveshare_p4_lcd43`, `waveshare_s3_lcd35b`, `waveshare_s3_lcd2`

### CRITICAL: Clean when switching boards

ESP-IDF's sdkconfig persists between builds. If you switch boards without cleaning, the old board's config (PSRAM speed, flash size, cache config) carries over and silently produces a broken build.

```bash
make clean    # removes build/, sdkconfig, managed_components, dependencies.lock
make docker-build BOARD=waveshare_p4_lcd43
```

**Always `make clean` before building for a different board.**

## Flash

The board must be connected via USB. The serial device is typically `/dev/ttyACM0`. Flash tools are inside Docker — pass the device and dialout group:

```bash
cd apps/qr_decoder/build
docker run --rm \
  --device=/dev/ttyACM0 \
  --group-add $(getent group dialout | cut -d: -f3) \
  -v $(pwd):/workspace \
  -w /workspace \
  ghcr.io/kdmukai-bot/seedsigner-micropython-builder-base:latest \
  bash -lc 'source /opt/toolchains/esp-idf/export.sh >/dev/null 2>&1 && esptool.py --chip esp32p4 -p /dev/ttyACM0 -b 460800 --before default_reset --after hard_reset write_flash @flash_args'
```

For ESP32-S3 boards, replace `--chip esp32p4` with `--chip esp32s3`, or use `--chip auto`.

### Alternative: dist target

```bash
make dist BOARD=waveshare_p4_lcd35
cd dist/waveshare_p4_lcd35
esptool.py --chip auto -b 460800 write_flash @flash_args
```

This only works if esptool is installed on the host (outside Docker).

## Monitor serial output

`idf.py monitor` requires a TTY and doesn't work from non-interactive Docker. Use raw serial capture instead:

```bash
docker run --rm \
  --device=/dev/ttyACM0 \
  --group-add $(getent group dialout | cut -d: -f3) \
  ghcr.io/kdmukai-bot/seedsigner-micropython-builder-base:latest \
  bash -c 'stty -F /dev/ttyACM0 115200 raw -echo && timeout 15 cat /dev/ttyACM0'
```

Filter for specific log tags:
```bash
... | grep -E 'cam_pipeline:|qr_decoder:|Error|panic'
```

Wait a few seconds after flash for boot to complete before capturing:
```bash
sleep 8 && docker run --rm ...
```

## Webcam screenshot

Capture a single frame from the attached USB webcam using ffmpeg (installed on host):

```bash
ffmpeg -f v4l2 -i /dev/video0 -frames:v 1 -update 1 -y /path/to/output.jpg
```

The `-update 1` flag is required for single-image output to a named file.

## Interactive Docker shell

For debugging or running arbitrary idf.py commands:

```bash
cd apps/qr_decoder
make docker-shell
# Inside container: idf.py menuconfig, idf.py size, etc.
```

## Docker run pattern (reference)

All Docker commands follow this pattern:

```bash
docker run --rm \
  --device=/dev/ttyACM0 \                              # pass serial device
  --group-add $(getent group dialout | cut -d: -f3) \   # dialout group for serial access
  -v $(pwd):/workspace \                                # mount working directory
  -w /workspace \                                       # set working directory
  ghcr.io/kdmukai-bot/seedsigner-micropython-builder-base:latest \
  bash -lc 'source /opt/toolchains/esp-idf/export.sh >/dev/null 2>&1 && <command>'
```

For build commands, the Makefile handles this — use `make docker-build` or `make docker-shell`. For flash and monitor, construct the Docker run manually as shown above.

## Common issues

| Issue | Cause | Fix |
|---|---|---|
| Wrong PSRAM speed / flash size after board switch | Stale sdkconfig from previous board | `make clean` before building |
| `Permission denied` on `/dev/ttyACM0` | Missing dialout group | Add `--group-add $(getent group dialout \| cut -d: -f3)` |
| `idf.py monitor` fails with "TTY required" | Non-interactive Docker | Use `stty + cat` raw serial capture instead |
| `esptool.py: command not found` | Not in Docker or ESP-IDF not sourced | Run inside Docker with `source export.sh` |
| Build uses wrong chip target | Stale sdkconfig | `make clean` and rebuild |
