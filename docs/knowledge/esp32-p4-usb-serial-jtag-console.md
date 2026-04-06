# ESP32-P4 USB Serial JTAG Console — Primary vs Secondary

## Problem

Setting `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` (primary console on USB Serial JTAG)
produces **no serial output** on `/dev/ttyACM0` for ESP32-P4 boards. The device
enumerates, esptool can flash via it, but runtime `ESP_LOGI` / `printf` output
never appears. `cat`, `picocom`, and Docker-based monitoring all fail silently.

This was misdiagnosed across multiple sessions as a hardware issue, a timing
issue (USB re-enumeration after reset), or a driver issue. It cost significant
debugging time because every landscape rotation experiment had to rely on
webcam screenshots instead of serial output.

## Root Cause

The ESP-IDF default console for ESP32-P4 is **UART0** (`CONFIG_ESP_CONSOLE_UART_DEFAULT=y`),
not USB Serial JTAG. This is different from ESP32-S3 where USB Serial JTAG is
more commonly used as primary console.

When `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` is explicitly set, it overrides the
UART0 default. But on the Waveshare P4 boards, USB Serial JTAG as the **primary**
console doesn't produce visible output. The exact mechanism isn't fully understood —
it may relate to the P4's USB-JTAG peripheral initialization timing, the ROM
serial port mapping (`CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM=6` for USB JTAG vs
`0` for UART0), or the blocking vs non-blocking output path.

## Solution

Use the **secondary** USB Serial JTAG console instead:

```
# In board sdkconfig.defaults:
CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y
```

Do NOT set `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`.

This results in:
- **Primary console**: UART0 at 115200 baud (ESP-IDF default, works if UART0 pins are connected)
- **Secondary console**: USB Serial JTAG — non-blocking output on `/dev/ttyACM0`

The secondary console provides ESP_LOG output over the same USB port used for
flashing. It's non-blocking, meaning the firmware doesn't stall if no host is
listening.

## Monitoring

```bash
# Configure port
stty -F /dev/ttyACM0 115200 raw -echo

# Capture output (wait a few seconds after flash/reset for USB re-enumeration)
timeout 15 cat /dev/ttyACM0

# Or use picocom for interactive monitoring
picocom -b 115200 --noreset /dev/ttyACM0
```

## Affected Boards

- Waveshare ESP32-P4 WiFi6 Touch LCD 4.3 (confirmed)
- Waveshare ESP32-P4 WiFi6 Touch LCD 3.5 (likely same behavior — same SoC, same USB-JTAG peripheral)

## Waveshare Demo Reference

Waveshare's own demo code (e.g., `02_HelloWorld`) does not set any `ESP_CONSOLE`
config, relying on the UART0 default. Their demos presumably output serial over
UART0 pins, not USB Serial JTAG.

## Key sdkconfig Values (working configuration)

```
CONFIG_ESP_CONSOLE_UART_DEFAULT=y           # Primary: UART0
CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y  # Secondary: USB JTAG on /dev/ttyACM0
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED=y    # Derived — enables the USJ driver
CONFIG_ESP_CONSOLE_UART=y                       # Derived
CONFIG_ESP_CONSOLE_UART_NUM=0                   # Derived
CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM=0        # Derived — ROM uses UART0
CONFIG_ESP_CONSOLE_UART_BAUDRATE=115200         # Derived
```
