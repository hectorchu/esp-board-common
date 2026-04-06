# Text Overlay Architecture

The QR decoder app needs to display text (FPS stats, QR decode results) on top of or alongside a live camera feed. The rendering approach varies by display interface and orientation because each combination has different constraints.

## The Four Overlay Paths

### 1. DSI Landscape (ST7701, P4 LCD 4.3)
**Approach:** Pre-rendered rotated canvas with bounding-box crop.

LVGL's `transform_rotation` is incompatible with `direct_mode` DSI rendering. Instead:
1. Render text into a hidden LVGL canvas in portrait coordinates
2. Find the tight bounding box of non-empty pixels
3. CPU-rotate only the cropped region 90° CCW
4. Display as an `lv_image` widget positioned in landscape coordinates

Supports both RGB565 (opaque, for gap-area overlays) and ARGB8888 (transparent, for camera-area overlays). The bounding-box crop minimizes the rotated region and avoids blending large transparent areas.

**Why not just use LVGL widgets?** Direct-mode DSI bypasses LVGL's rendering pipeline for the camera feed. LVGL widgets render on top, but `transform_rotation` on widgets doesn't work with direct-mode. The pre-rendered canvas + manual rotation is the workaround.

### 2. SPI Portrait Dummy-Draw (ST7796, P4 LCD 3.5)
**Approach:** Direct font rendering into panel gap areas.

SPI displays tear when LVGL renders concurrently with camera frame pushes. Dummy-draw mode (`lvgl_port_stop()`) halts LVGL entirely — no widgets, no timers. Text is rendered by:
1. Using LVGL font glyph APIs to render directly into RGB565 gap buffers
2. Byte-swapping through an internal-RAM DMA buffer
3. Writing to the panel via `esp_lcd_panel_draw_bitmap()` — one call per 80px gap

Text lives in the black gap areas above/below the camera square. The camera feed is never modified. Gap writes happen only when text changes (not per-frame), and are deferred to the camera task's overlay callback to avoid SPI bus contention with the camera frame push.

**Why not composite onto the camera feed?** Two reasons:
- RGB565 overlay compositing uses 0x0000 (black) as the transparency key — no true alpha blending, just hard pixel replacement. Anti-aliased glyph edges blend against black, creating dark fringes on bright camera backgrounds.
- Writing text to the gap areas is simpler, avoids per-frame composite overhead, and matches the visual layout of the DSI portrait path.

**Why one large DMA transfer per gap instead of stripes?** Striped gap writes (4×20-line transfers per 80px gap) caused column-level pixel artifacts — the panel's internal addressing appeared to drift between rapid successive writes. Using a single 80-line DMA transfer per gap eliminated the artifacts. This costs ~51KB of internal RAM for the DMA buffer.

### 3. DSI Portrait (ST7701) — Theoretical
**Approach:** Standard LVGL label widgets.

LVGL is running normally (not dummy-draw). Text is rendered as `lv_label` widgets positioned over the camera feed or in gap areas. Auto-hide via LVGL timers.

No board currently uses this configuration, but the code path exists.

### 4. SPI Landscape — Future
**Approach:** TBD. Will likely need ARGB8888 overlay layers composited over the camera feed, since the camera square doesn't leave usable gap areas in landscape orientation on small panels. This is the motivating case for eventually moving from RGB565 to ARGB overlay buffers.

## Future Direction: Unified Display Zone Renderer

The current overlay code is tightly coupled to the QR decoder app's specific needs (FPS stats + QR result text). The three implementations (rotated canvas in main.c, overlay_text module, LVGL widgets) should eventually be generalized into a **display zone rendering layer** in esp-board-common that:

- Defines **zones** (gap areas, camera-area overlays, full-screen regions) based on panel geometry and camera placement
- Provides a **backend-agnostic API** for placing content into zones — not just text, but any app-defined UI element
- Selects the **rendering backend** (direct glyph + panel write, LVGL canvas + rotation, LVGL widgets) based on display interface (SPI/DSI), orientation, and whether LVGL is running
- Moves the `#if BOARD_DISPLAY_DRIVER` compile-time dispatch out of app code and into the module
- Supports apps beyond QR scanning — any app that needs to render UI alongside a camera feed or on specific screen regions

### Current state (as of 2026-04-06)
- `src/overlay_text.c/.h` — SPI portrait gap-area backend, reusable component
- DSI landscape rotated canvas — embedded in `apps/qr_decoder/main/main.c`, needs extraction
- DSI portrait LVGL widgets — inline in main.c, trivial
- Shared rendering utilities (glyph_alpha, rotation kernels, bbox finder) not yet factored out

### What extraction would look like
```
src/camera_overlay.h              — unified API: create, set_content, zone config
src/camera_overlay_gap.c          — SPI gap-area backend (current overlay_text.c)
src/camera_overlay_rotated.c      — DSI rotated canvas (extracted from main.c)
src/camera_overlay_common.c       — shared: RGB565 helpers, rotation kernels, bbox
```

This is deferred — the current per-path implementations work and are documented. Unification should happen when the fourth path (SPI landscape) is implemented, since it will need to combine elements from both existing backends.

## Key Technical Constraints

### LVGL Font Bitmap API (v9.5.0)
The direct font renderer bypasses LVGL's normal draw pipeline. Three API gotchas:

1. **`lv_font_get_glyph_bitmap()`** — The public wrapper saves `req_raw_bitmap`, sets it to 0, calls the internal function, then restores. This means the internal function always sees `req_raw_bitmap=0` and tries to convert to A8 format using the `draw_buf` parameter. Passing `draw_buf=NULL` crashes.

2. **`lv_font_get_glyph_static_bitmap()`** — Checks `font->static_bitmap == 1` and returns NULL if not set. Standard montserrat fonts (non-aligned variants) do NOT set this flag.

3. **Working approach:** Call `font->get_glyph_bitmap()` function pointer directly with `g.req_raw_bitmap = 1`. This bypasses the wrapper and gets the raw bitmap without needing a draw_buf.

### Raw A4 Bitmap Format (stride=0)
LVGL's built-in font bitmaps at 4bpp are stored as a **flat continuous nibble stream** — no per-row byte padding when stride is 0. For a glyph with `box_w=13`:
- Row 0: nibbles 0–12 (bytes 0–6, using high nibble of byte 6)
- Row 1: nibbles 13–25 (starts at LOW nibble of byte 6)

Treating each row as byte-aligned (`ceil(box_w/2)` bytes per row) produces sheared/garbled glyphs. The correct indexing is `nibble_pos = py * box_w + px`.

### SPI Bus Contention
All `esp_lcd_panel_draw_bitmap()` calls on an SPI panel share the SPI bus. Concurrent calls from different tasks serialize via the bus mutex, but this can cause one task to block indefinitely if the other is doing a large transfer.

**Rule:** All panel writes for a given SPI display must happen from the same task. The overlay text module defers gap writes to the camera task's overlay callback (which runs just before the camera frame push in `lvgl_display_push_frame`). This ensures gap writes and camera frame writes never contend on the SPI bus.
