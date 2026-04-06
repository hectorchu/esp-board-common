# LVGL transform_rotation Incompatible with direct_mode on DSI Panels

## Problem

Setting `lv_obj_set_style_transform_rotation()` on any widget (even a small label)
causes the LVGL rendering task to hang when the display is configured with
`direct_mode + avoid_tearing` on a MIPI-DSI DPI panel. The display freezes on the
first rendered frame and never updates again.

Serial output confirms: `disp=0.0fps(skip 100%)` — the camera pipeline's
`lvgl_port_lock(1)` try-lock never succeeds because the LVGL task holds the lock
permanently.

## Root Cause

LVGL's `transform_rotation` style triggers `LV_LAYER_TYPE_TRANSFORM` on the widget
(see `calculate_layer_type()` in `lv_obj_style.c`). During rendering, `lv_obj_refr()`
creates a temporary layer via `lv_draw_layer_create()`, renders the widget into it,
then composites the layer back with the rotation applied via `lv_draw_layer()`.

This layer-based compositing is **incompatible with `direct_mode` rendering**:

- In `direct_mode`, LVGL renders directly into the DPI panel's hardware framebuffers
  (PSRAM, managed by the DPI controller). There are no separate draw buffers.
- Transform layers need a temporary ARGB8888 buffer, rendered separately, then
  composited back into the framebuffer as a rotated image.
- The compositing step (image draw with rotation into a DPI framebuffer) conflicts
  with direct_mode's assumptions about in-place rendering and vsync coordination.

This is a known LVGL limitation, not a bug in our code:

- [esp-bsp #400](https://github.com/espressif/esp-bsp/issues/400): "When using
  software rotation, you cannot use either `direct_mode` nor `full_refresh`"
- [lvgl #3829](https://github.com/lvgl/lvgl/issues/3829): "Can't rotate when
  enable direct-mode"
- [LVGL Draw Layers docs](https://docs.lvgl.io/master/main-modules/draw/draw_layers.html):
  Transform layers allocate buffers "large enough to render the entire transformed
  area without limits"

## What We Tested

On the Waveshare ESP32-P4 LCD 4.3 (ST7701 MIPI-DSI, 480x800, `direct_mode +
avoid_tearing`, triple-buffered DPI framebuffers):

1. **Portrait overlays, no rotation** — `cam=18fps disp=10fps` (working baseline)
2. **Portrait overlays with `transform_rotation=900`** — `cam=23fps disp=0.0fps
   (skip 100%)` (display frozen, LVGL task hung)
3. **Portrait overlays, no rotation, `LANDSCAPE=1` build flag** — `cam=18fps
   disp=10fps` (confirms the Kconfig flag itself has no effect)

The hang occurs with even the smallest widget (auto-sized "---" label). It is not
a memory allocation size issue — it is the fundamental incompatibility between
transform layers and direct_mode rendering.

## What Works

- `transform_rotation` works on displays using **partial mode** (no `direct_mode`)
- Display-level rotation via `lv_display_set_rotation()` + esp_lvgl_port's
  `sw_rotate` flag works in partial mode with PPA hardware acceleration
- Standard LVGL widgets (labels, images, containers) without transforms work
  fine in direct_mode

## Implications for Landscape Camera Overlay

For the SeedSigner camera scanning use case on DSI panels, rotated LVGL widgets
cannot be used while in direct_mode. Alternatives:

1. **Pre-render rotated text into an `lv_canvas`** — draw text at the desired
   angle into a pixel buffer, then display it as a normal `lv_image` widget
   (no transform layer needed, compatible with direct_mode).

2. **Use dummy_draw mode** — bypass LVGL entirely during camera scanning.
   Push camera frames directly via `draw_bitmap`. No LVGL overlays during
   scanning; handle all UI feedback before/after the camera phase.

3. **Accept portrait-oriented overlays** — keep text in portrait orientation
   during camera scanning. Simple, no performance cost, acceptable for minimal
   overlays like a progress bar.

4. **Switch to partial mode for the camera screen** — drop direct_mode, use
   the port's sw_rotate + PPA. But this was measured at ~2fps in earlier
   testing due to per-band overhead (see `project_landscape_support` memory).

## Affected Boards

Any board using MIPI-DSI DPI panels with `direct_mode + avoid_tearing`:
- Waveshare ESP32-P4 WiFi6 Touch LCD 4.3 (ST7701, confirmed)
- Any future DPI panel board with similar configuration

SPI boards (ST7796, ST7789, AXS15231B) use partial mode and are NOT affected —
`transform_rotation` should work fine on those displays.
