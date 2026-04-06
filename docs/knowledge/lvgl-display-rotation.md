# LVGL Display Rotation on ESP32 — Landscape Support

## Overview

SeedSigner targets landscape orientation on boards with natively portrait panels.
This document captures the findings from extensive testing of landscape rotation
approaches on ESP32-S3 and ESP32-P4 boards with SPI and MIPI-DSI (DPI) displays.

The core challenge: DPI panels (like the ST7701 on the P4 LCD 4.3) have no
hardware rotation support (no MADCTL). Every pixel must be physically rearranged
in software or via DMA before reaching the panel. How and when this rotation
happens determines whether landscape is usable or unusable.

---

## Per-Board Rotation Strategy

| Board | Panel | Interface | Rotation Mechanism | Cost | Notes |
|---|---|---|---|---|---|
| P4 LCD 3.5 | ST7796 | SPI | MADCTL (`swap_xy` + `mirror`) | Zero | Hardware register, instant |
| P4 LCD 4.3 | ST7701 | MIPI-DSI DPI | Software/PPA in flush callback | Moderate | See DPI section below |
| S3 LCD 3.5 | ST7796 | SPI | MADCTL (`swap_xy` + `mirror`) | Zero | Same as P4 LCD 3.5 |
| S3 LCD 3.5B | AXS15231B | QSPI | CPU rotation in flush (153K px) | ~6ms/frame | No PPA on S3 |
| S3 LCD 2 | ST7789 | SPI | MADCTL (`swap_xy` + `mirror`) | Zero | Hardware register |

**SPI panels** support hardware rotation via MADCTL — a register write that tells
the panel controller to read its internal GRAM in rotated order. Zero CPU cost.
Landscape is trivially achieved with `esp_lcd_panel_swap_xy()` and
`esp_lcd_panel_mirror()`.

**DPI panels** (MIPI-DSI) are "dumb" streaming interfaces with no internal GRAM
and no MADCTL. The panel displays whatever bytes arrive in its fixed native scan
order. Rotation must be done by rearranging pixels before they reach the panel.

---

## DPI Rotation: Where It Happens Matters

There are three places rotation can occur. Only one is fast:

### 1. Render-time rotation (SLOW)

LVGL writes pixels at rotated coordinates during its draw calls. Every fill,
text glyph, and image blit goes through coordinate transformation. This
destroys cache performance — horizontal rows in logical space become vertical
columns in the buffer, causing cache misses on every pixel write.

**The esp_lvgl_adapter's `ROTATE_90`/`ROTATE_270` config does this.** It sets
LVGL's display rotation which causes per-pixel coordinate remapping during
rendering. Even with PPA in the flush callback, the render phase dominates.

**Result: 0.5-4 fps on P4 LCD 4.3 (480x800 panel).**

### 2. Flush-time rotation (FAST with PPA)

LVGL renders in landscape coordinates with fast sequential writes (cache-
friendly). After rendering each partial band, the flush callback rotates the
band and sends it to the panel. With the ESP32-P4's PPA (Pixel Processing
Accelerator), this rotation is hardware DMA — near-instant.

**The esp_lvgl_port library does this.** LVGL renders normally, the flush
callback PPA-rotates each partial band, and `draw_bitmap` places it on the
panel. The rendering is identical to portrait speed; only a fast PPA step is
added per band.

**Result: comparable to portrait fps (tested with C-module screens —
screensaver animation, scrolling text).**

### 3. Panel hardware rotation (FREE but DPI doesn't support it)

MADCTL register changes the panel's scan direction. Only works for SPI panels
with internal GRAM. DPI panels don't have this capability.

---

## esp_lvgl_port vs esp_lvgl_adapter

### esp_lvgl_port (recommended for DPI landscape)

**Architecture:**
- Uses the DPI panel's own framebuffers via `esp_lcd_dpi_panel_get_frame_buffer()`
- No separate buffer pipeline — hardware triple-buffering handles vsync
- Simple vsync semaphore: `xSemaphoreTake` in flush, `xSemaphoreGiveFromISR` in
  the vsync callback
- Flush-time rotation: LVGL renders normally, flush callback PPA-rotates and
  calls `draw_bitmap`
- LVGL lock held only during `lv_timer_handler()`, no blocking hardware waits

**Rotation in flush callback (from esp_lvgl_port source):**
```c
// In flush callback:
if (disp_ctx->flags.sw_rotate && rotation > LV_DISPLAY_ROTATION_0) {
    if (disp_ctx->ppa_handle) {
        // PPA hardware rotation — fast, DMA-based
        lvgl_port_ppa_rotate(disp_ctx->ppa_handle, &rotate_cfg);
        color_map = lvgl_port_ppa_get_output_buffer(disp_ctx->ppa_handle);
    } else {
        // Software fallback — lv_draw_sw_rotate() into scratch buffer
        lv_draw_sw_rotate(color_map, disp_ctx->draw_buffs[2], ...);
        color_map = disp_ctx->draw_buffs[2];
    }
}
esp_lcd_panel_draw_bitmap(panel, x1, y1, x2, y2, color_map);
```

**Why it's fast:**
1. LVGL renders into small internal-RAM partial buffers (cache-friendly)
2. PPA rotates each partial band via hardware DMA (~instant)
3. `draw_bitmap` submits to DPI panel (non-blocking, hardware triple-buffered)
4. LVGL lock released quickly — camera pipeline can push frames

**What it lacks:**
- No `dummy_draw` mode for tear-free camera on SPI panels
- This can be implemented as a lightweight wrapper (see below)

### esp_lvgl_adapter (problematic for DPI landscape)

**Architecture:**
- Implements its own buffer pipeline on top of the DPI panel (busy/empty lists,
  spinlocks, STAILQ queues)
- `display_bridge_pipeline_wait_free_buf()` blocks with `portMAX_DELAY` waiting
  for a buffer, **while holding the LVGL mutex**
- Render-time rotation via LVGL display rotation (per-pixel coordinate remapping
  during draw calls)

**The critical bottleneck (from adapter source):**
```c
// In flush_partial_rotate() and flush_full_rotate():
esp_err_t ret = display_lcd_blit_full(panel, &impl->runtime, impl->draw_fb);
display_bridge_pipeline_mark_buf_busy(&impl->pipeline, impl->disp_fb);

// THIS BLOCKS while holding the LVGL mutex:
struct display_pipeline_buf *next = display_bridge_pipeline_wait_free_buf(&impl->pipeline);
// wait_free_buf() calls: ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
// Waits up to 33ms (one DPI refresh period) for vsync ISR to free a buffer
```

**Why it's slow:**
1. Render-time rotation: every pixel write during LVGL rendering is coordinate-
   transformed (cache-hostile)
2. `pipeline_wait_free_buf` blocks ~33ms per frame while holding the LVGL lock
3. Camera pipeline's `try_lock(0)` fails constantly (lock held 93-98% of the time)
4. Display fps drops to 0.5-4 fps

**What it provides:**
- `dummy_draw` mode for tear-free camera display on SPI panels
- `esp_lv_adapter_set_dummy_draw()` pauses LVGL rendering
- `esp_lv_adapter_dummy_draw_blit()` sends camera frames directly to panel

### GitHub issues confirming this

- **esp-iot-solution #667**: P4 user reports adapter is sluggish; switching to
  esp_lvgl_port fixes it. Still open as of 2026-04.
- **esp-bsp #400**: SW rotation on P4 + MIPI-DSI "noticeably slower" with adapter.
- **esp-bsp PR #352**: PPA in esp_lvgl_port gives 10x speedup (hundreds of ms to
  10-20ms) for full-screen rotation.
- **esp-iot-solution #628**: Espressif confirms adapter will "eventually supersede"
  port but hasn't resolved the performance gap.
- **esp-iot-solution #380**: ESP32-S3 + ST7701 RGB, SW rotation drops to 2-3 fps.
  Espressif: "no good solution yet for screen rotation on ESP32-S3" for RGB panels.

---

## PPA (Pixel Processing Accelerator) — ESP32-P4 Only

The ESP32-P4 has a PPA hardware DMA engine that can rotate, scale, and mirror
pixel buffers without CPU involvement. Key details:

- **Buffer alignment**: input and output buffers must be 128-byte aligned
  (L2 cache line size). Use `heap_caps_aligned_alloc(128, size, caps)`.
- **Output buffer size**: must also be 128-byte aligned. Round up:
  `((size + 127) & ~127u)`.
- **Color format**: RGB565 (`PPA_SRM_COLOR_MODE_RGB565`)
- **Blocking mode**: `PPA_TRANS_MODE_BLOCKING` waits for DMA completion
- **Rotation angles**: 0, 90, 180, 270 via `PPA_SRM_ROTATION_ANGLE_*`
- **Registration**: `ppa_register_client()` with `PPA_OPERATION_SRM`
- **API**: `ppa_do_scale_rotate_mirror(client, &config)`
- **Performance**: rotates 800x50 band in <1ms (hardware DMA)

The camera pipeline already uses PPA for camera frame rotation — see
`components/esp-camera-pipeline/src/esp_cam_pipeline.c` for a working example.

**Important Kconfig**: `ESP_MM_CACHE_MSYNC_C2M_CHUNKED_OPS` should be enabled
to avoid screen artifacts when PPA rotates large buffers (per esp-bsp PR #352).

---

## Implementing Dummy Draw Without esp_lvgl_adapter

The adapter's `dummy_draw` mode does two things:
1. Pauses LVGL rendering (prevents LVGL from drawing over camera frames)
2. Provides `esp_lv_adapter_dummy_draw_blit()` to send camera frames directly
   to the panel via `esp_lcd_panel_draw_bitmap()`

A lightweight equivalent for esp_lvgl_port:

```c
// board_dummy_draw.h
void board_dummy_draw_set(lv_display_t *disp, bool enable);
bool board_dummy_draw_is_active(void);
esp_err_t board_dummy_draw_blit(lv_display_t *disp,
    int x, int y, int w, int h, const void *data, bool wait);

// In the LVGL flush callback (registered via lv_display_set_flush_cb):
if (board_dummy_draw_is_active()) {
    lv_disp_flush_ready(disp);  // skip LVGL rendering output
    return;
}
// ... normal flush with rotation ...

// Camera pipeline calls board_dummy_draw_blit() which calls
// esp_lcd_panel_draw_bitmap() directly, bypassing LVGL.
```

The existing `board_pipeline_display_lvgl.c` already implements the camera-side
logic — it just needs the adapter-specific calls replaced with the lightweight
equivalents above.

---

## Camera Rotation vs Display Rotation

These are independent concerns:

- **Camera rotation** (`BOARD_CAMERA_ROTATION`): compensates for the physical
  mounting angle of the camera module on the board. Uses PPA hardware on P4,
  set per-board in `board_config.h`. Does NOT change with portrait/landscape.

- **Display rotation**: compensates for the viewing orientation. For DPI panels,
  this is flush-time PPA rotation (esp_lvgl_port) or MADCTL (SPI panels).

A live camera preview is like a see-through window — rotating the board doesn't
change what the camera sees. The display rotation only affects LVGL-rendered
elements (text, UI widgets), not the live camera feed.

---

## Compile-Time Landscape Support

The landscape infrastructure is in place (but the actual rotation implementation
needs to use esp_lvgl_port, not the adapter):

- **Kconfig**: `CONFIG_BOARD_LANDSCAPE` in `Kconfig` (default: off)
- **Build flag**: `make docker-build BOARD=waveshare_p4_lcd43 LANDSCAPE=1`
- **CMake**: `sdkconfig.landscape` appended when `-DLANDSCAPE=1`
- **Macros**: `BOARD_LANDSCAPE`, `BOARD_DISP_H_RES`, `BOARD_DISP_V_RES` in
  `board.h` — swap physical dimensions for landscape
- **Pipeline**: `board_pipeline_default_config()` uses logical dimensions

---

## Migration Plan: Adapter to Port for DPI Landscape

1. Replace `espressif/esp_lvgl_adapter` with `espressif/esp_lvgl_port` in
   `idf_component.yml` and `CMakeLists.txt`
2. Rewrite `board_init.c` display/touch/task setup:
   - `esp_lv_adapter_init()` → `lvgl_port_init()`
   - `esp_lv_adapter_register_display()` → `lvgl_port_add_disp()` (SPI) or
     `lvgl_port_add_disp_dsi()` (MIPI-DSI)
   - `esp_lv_adapter_register_touch()` → `lvgl_port_add_touch()`
   - `esp_lv_adapter_lock/unlock()` → `lvgl_port_lock/unlock()`
   - For landscape: set `flags.sw_rotate = true` and `rotation = LV_DISPLAY_ROTATION_90`
     (or 270, determined by physical board orientation)
   - Enable PPA via `LVGL_PORT_ENABLE_PPA` Kconfig
3. Implement lightweight `board_dummy_draw` for SPI camera support
4. Update `board_pipeline_display_lvgl.c` to use port lock API and board_dummy_draw
5. Update app code (`main.c`) to use `lvgl_port_lock/unlock()`

---

## Key Lessons

1. **Rotation location matters more than rotation speed.** PPA hardware rotation
   is near-instant, but if the rendering phase is slow (render-time rotation),
   the PPA flush doesn't help. Flush-time rotation is the correct architecture.

2. **The LVGL lock is the bottleneck, not rotation.** Any blocking wait inside
   the flush callback (vsync, buffer pipeline) holds the LVGL mutex and starves
   other tasks (camera pipeline, touch input). The flush must return quickly.

3. **DPI panels need different treatment than SPI panels.** SPI has MADCTL
   (free rotation) but tears during camera streaming. DPI doesn't tear but has
   no hardware rotation. The libraries handle these tradeoffs differently.

4. **Buffer allocation matters.** Full-frame PSRAM buffers are too slow for
   LVGL rendering (~2fps). Small internal-RAM partial buffers (e.g., 800x50)
   with PPA rotation per band match portrait rendering speed.

5. **esp_lvgl_port's simplicity is its strength.** Direct use of DPI hardware
   framebuffers, simple vsync semaphore, flush-time rotation. The adapter's
   additional abstraction (buffer pipeline) introduces blocking that's fatal
   for real-time camera applications.
