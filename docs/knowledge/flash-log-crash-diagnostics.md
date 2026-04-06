# Flash Log & Crash Diagnostics System

Applies to all ESP-IDF projects using esp-board-common, not just apps in this repo. The `board_log_flash` component and partition/Kconfig patterns should be applied to any ESP32 project where crash diagnostics or complete boot log capture are needed.

## Problem
ESP32 boards using USB Serial/JTAG for console lose the first 2-5 seconds of boot output when the board resets. The USB device disconnects and re-enumerates, and the host isn't listening yet. This makes debugging boot-time crashes extremely difficult — you can't see the backtrace. This affects all ESP32 targets (S3, P4, etc.) when USB Serial/JTAG is the console.

## Solution: Three-Layer Log Capture

### Layer 1: Flash Log (board_log_flash)
**What**: Tees all `ESP_LOGx` output to a `log_store` flash partition (64KB, subtype 0x42). Each log line is flushed to flash within 500ms via a background task. Call `board_log_flash_dump()` to read back the current session's complete log at any time.

**Architecture**: The vprintf hook runs in any task context and only writes to an internal-SRAM ring buffer (8KB). A dedicated flush task with an internal-RAM stack (created via `xTaskCreateStatic` + `heap_caps_malloc(MALLOC_CAP_INTERNAL)`) handles the actual flash writes. This two-stage design avoids the PSRAM stack + flash write conflict.

**Survives**: Power cycles and hard resets.

**Previous boot**: On init, the previous boot's flash log is read into PSRAM and available via `board_log_flash_dump_previous()`.

### Layer 2: Noinit Ring Buffer
**What**: The ring buffer struct is declared with `__NOINIT_ATTR`, placing it in a .noinit section of internal SRAM. On boot, `board_log_flash_init()` checks `esp_reset_reason()` — if it was a panic, watchdog, or brownout, the buffer contents from the previous boot are still valid and are dumped to serial.

**Survives**: Soft resets (panic, watchdog, brownout). Does NOT survive power cycles.

**Lightweight**: No flash writes needed for crash recovery — just reads from RAM.

### Layer 3: Core Dump (ESP-IDF built-in)
**What**: `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y` with `CONFIG_ESP_COREDUMP_CAPTURE_DRAM=y`. On crash, ESP-IDF's panic handler writes the full system state to a `coredump` partition (64KB). Includes task backtraces with file/line numbers, register state, stack usage per task, and all internal DRAM contents (which includes the noinit ring buffer).

**Survives**: Soft resets. The core dump persists until overwritten by the next crash.

**Most valuable for debugging**: Gives exact crash location, full call chain, and every task's state at the moment of crash.

## The PSRAM Stack Constraint

This is the fundamental constraint that shaped the entire architecture.

Flash operations (`esp_partition_read/write/erase`) disable the SPI cache, making PSRAM inaccessible. Any task whose stack is in PSRAM will crash with `assert failed: esp_task_stack_is_sane_cache_disabled` if it calls flash APIs.

On ESP32-P4 with 32MB PSRAM, the default heap allocator places large allocations (including FreeRTOS task stacks) in PSRAM. Tasks created with `xTaskCreate` may get PSRAM stacks silently.

**Rules**:
- The vprintf hook must NEVER call flash APIs — it runs in any task's context
- Any task that reads/writes flash must use `xTaskCreateStatic` with stack allocated via `heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)`
- The `esp_register_shutdown_handler` callback runs in whatever task triggered the shutdown — if that task has a PSRAM stack, direct flash writes will crash

## Serial Output Interleaving

When dumping the flash log, both the dump output and live `ESP_LOGx` output go to the same serial port. Without protection, log lines get corrupted mid-character.

**Fix**: `board_log_flash_dump()` sets a `s_serial_paused` flag. The vprintf hook skips `s_orig_vprintf` (serial output) while paused but still writes to the ring buffer (flash capture). No log data is lost — just a few seconds of missing live serial, all captured in flash.

## Integration Checklist

1. Add `coredump` and `log_store` partitions to `partitions.csv`
2. Add Kconfig flags to `sdkconfig.defaults` (coredump + flash log)
3. Call `board_log_flash_init()` as the first thing in `app_main()`
4. Add a delayed `board_log_flash_dump()` call (8s after boot) for development — use `xTaskCreateStatic` with internal-RAM stack
5. Read core dumps from host with `python -m esp_coredump --port /dev/ttyACM0 info_corefile <app>.elf` (requires GDB on PATH)

## What We Tried That Didn't Work

- **Direct flash writes from vprintf hook**: Crashed immediately because camera/LVGL/QR tasks have PSRAM stacks
- **`esp_register_shutdown_handler` with direct flash writes**: Same PSRAM crash — the handler runs in the panicking task's context
- **LVGL canvas rendering from FreeRTOS tasks**: LVGL's draw dispatch depends on the timer task, which is stopped in dummy-draw mode. Canvas renders queued but never dispatched.
- **`getchar()` serial listener for on-demand dumps**: Works for ESP-IDF native apps but conflicts with MicroPython REPL in production builds
