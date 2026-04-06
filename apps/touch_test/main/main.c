/**
 * Touch Test — touch verification + visual feedback for board bringup.
 *
 * Small corner buttons (TL/TR/BL/BR) verify touch coordinate mapping.
 * A wireframe icosahedron responds to gestures:
 *   - Tap:           burst flash + brief scale pulse
 *   - Swipe:         spin with inertia (speed from swipe velocity), decays to stop
 *   - Pinch:         zoom in/out
 *   - Two-finger swipe: translate XY, snaps back to center
 *
 * Edges are drawn back-to-front with z-depth color mapping for readability.
 * Swipe timing metrics track best duration and consecutive gap.
 */
#include <stdio.h>
#include <math.h>
#include <limits.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board.h"
#include "board_config.h"
#include "board_backlight.h"
#include "esp_lcd_touch.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#if LV_USE_GESTURE_RECOGNITION
#include "indev/lv_indev_gesture.h"
#endif

static const char *TAG = "touch_test";

/* ── Quadrant buttons ── */

#define NUM_QUADRANTS 4
#define CORNER_BTN_SIZE 70

typedef struct {
    lv_obj_t *btn;
    lv_obj_t *label;
    const char *name;
} quadrant_t;

static quadrant_t quadrants[NUM_QUADRANTS];

static const lv_color_t COLOR_IDLE   = { .blue = 0x40, .green = 0x40, .red = 0x40 };
static const lv_color_t COLOR_ACTIVE = { .blue = 0x00, .green = 0xCC, .red = 0x00 };

/* ── Icosahedron geometry ── */

#define ICO_VERTS 12
#define ICO_EDGES 30

/* Golden ratio */
#define PHI 1.6180339887f

/* Unit icosahedron vertices (will be scaled at render time) */
static const float ico_verts[ICO_VERTS][3] = {
    { 0,  1,  PHI}, { 0, -1,  PHI}, { 0,  1, -PHI}, { 0, -1, -PHI},
    { 1,  PHI, 0}, {-1,  PHI, 0}, { 1, -PHI, 0}, {-1, -PHI, 0},
    { PHI, 0,  1}, {-PHI, 0,  1}, { PHI, 0, -1}, {-PHI, 0, -1},
};

static const uint8_t ico_edges[ICO_EDGES][2] = {
    {0,1}, {0,4}, {0,5}, {0,8}, {0,9},
    {1,6}, {1,7}, {1,8}, {1,9},
    {2,3}, {2,4}, {2,5}, {2,10}, {2,11},
    {3,6}, {3,7}, {3,10}, {3,11},
    {4,5}, {4,8}, {4,10},
    {5,9}, {5,11},
    {6,7}, {6,8}, {6,10},
    {7,9}, {7,11},
    {8,10}, {9,11},
};

/* ── 3D state ── */

/* Rotation matrix — accumulated in screen space to avoid gimbal lock.
 * New rotations are LEFT-multiplied so axes stay aligned with the viewport.
 * Initialized with a slight tilt so the shape isn't face-on at start. */
static float rot_mat[3][3] = {
    {1, 0, 0},
    {0, 1, 0},
    {0, 0, 1},
};
static bool rot_mat_initialized = false;

static float spin_vx = 0, spin_vy = 0;     /* screen-space angular velocity (rad/frame) */
static float zoom = 1.0f;

/* Swipe vector tracking */
static lv_point_t press_point = {0, 0};  /* screen position at finger-down */
static bool in_drag_mode = false;        /* true once slow drag is confirmed */
static bool gesture_fired = false;       /* true if swipe/drag/pinch happened this touch */

/* Tap burst state */
static float burst_scale = 0;          /* additive scale pulse, decays to 0 */
static float burst_brightness = 0;     /* additive color brightness, decays to 0 */

#define SPIN_DECAY 0.96f                /* per-frame velocity decay */
#define SPIN_STOP_THRESHOLD 0.001f
#define BURST_DECAY 0.85f

/* Apply a screen-space rotation (around viewport X and Y) to the rotation matrix.
 * Left-multiplies so the axes always correspond to the screen. */
static void apply_screen_rotation(float ax, float ay)
{
    float cx = cosf(ax), sx = sinf(ax);
    float cy = cosf(ay), sy = sinf(ay);

    /* Combined Rx(ax) * Ry(ay) */
    float r[3][3] = {
        { cy,      0,   sy     },
        { sx * sy, cx, -sx * cy},
        {-cx * sy, sx,  cx * cy},
    };

    /* new_rot = r * rot_mat */
    float tmp[3][3];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            tmp[i][j] = r[i][0] * rot_mat[0][j]
                      + r[i][1] * rot_mat[1][j]
                      + r[i][2] * rot_mat[2][j];
        }
    }
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            rot_mat[i][j] = tmp[i][j];
}

/* ── Canvas ── */

static lv_obj_t *canvas = NULL;
static int canvas_w = 0, canvas_h = 0;
static int canvas_cx = 0, canvas_cy = 0;

/* ── Swipe metrics ── */

static int64_t press_start_us = 0;
static int64_t last_swipe_us = 0;
static int32_t best_duration_ms = INT32_MAX;
static int32_t best_gap_ms = INT32_MAX;
static uint32_t swipe_count = 0;

/* ── UI labels ── */

static lv_obj_t *gesture_label = NULL;
static lv_obj_t *stats_label = NULL;
static lv_obj_t *coord_label = NULL;

/* ── Rendering ── */

static void render_icosahedron(void)
{
    if (!canvas) return;

    /* Clear canvas */
    lv_canvas_fill_bg(canvas, lv_color_hex(0x000000), LV_OPA_COVER);

    /* Transform vertices using accumulated rotation matrix */
    float proj[ICO_VERTS][2];  /* screen x, y */
    float depth[ICO_VERTS];     /* z after rotation */

    float effective_scale = (zoom + burst_scale) * (float)canvas_w * 0.18f;

    for (int i = 0; i < ICO_VERTS; i++) {
        float x = ico_verts[i][0];
        float y = ico_verts[i][1];
        float z = ico_verts[i][2];

        /* Apply rotation matrix */
        float x1 = rot_mat[0][0] * x + rot_mat[0][1] * y + rot_mat[0][2] * z;
        float y1 = rot_mat[1][0] * x + rot_mat[1][1] * y + rot_mat[1][2] * z;
        float z2 = rot_mat[2][0] * x + rot_mat[2][1] * y + rot_mat[2][2] * z;

        depth[i] = z2;

        /* Perspective projection */
        float persp = 4.0f / (4.0f + z2 * 0.3f);
        proj[i][0] = x1 * persp * effective_scale + canvas_cx;
        proj[i][1] = y1 * persp * effective_scale + canvas_cy;
    }

    /* Sort edges by average z-depth (back to front) */
    float edge_depth[ICO_EDGES];
    int edge_order[ICO_EDGES];
    for (int i = 0; i < ICO_EDGES; i++) {
        edge_depth[i] = (depth[ico_edges[i][0]] + depth[ico_edges[i][1]]) * 0.5f;
        edge_order[i] = i;
    }
    /* Simple insertion sort — 30 elements */
    for (int i = 1; i < ICO_EDGES; i++) {
        int key = edge_order[i];
        float kd = edge_depth[key];
        int j = i - 1;
        while (j >= 0 && edge_depth[edge_order[j]] > kd) {
            edge_order[j + 1] = edge_order[j];
            j--;
        }
        edge_order[j + 1] = key;
    }

    /* Each edge gets a fixed hue, brightness modulated by z-depth.
     * 30 edges spread across the hue wheel so adjacent edges contrast. */

    /* z range for icosahedron is roughly [-PHI, +PHI] */
    float z_min = -PHI * 1.2f, z_range = PHI * 2.4f;

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    for (int ei = 0; ei < ICO_EDGES; ei++) {
        int e = edge_order[ei];
        int v0 = ico_edges[e][0], v1 = ico_edges[e][1];

        /* Depth: 0=far, 1=near */
        float z_avg = (depth[v0] + depth[v1]) * 0.5f;
        float t = (z_avg - z_min) / z_range;
        if (t < 0) t = 0;
        if (t > 1) t = 1;

        /* Fixed hue per edge (0..360), spread to maximize contrast */
        float hue = (float)(e * 360 / ICO_EDGES);

        /* HSV to RGB — saturation=0.8, value=brightness from z-depth.
         * Far edges darken significantly (min brightness 0.12),
         * near edges go full bright. */
        float brightness = 0.12f + t * 0.88f + burst_brightness;
        if (brightness > 1.0f) brightness = 1.0f;
        float sat = 0.8f;

        float c = brightness * sat;
        float h6 = hue / 60.0f;
        float x = c * (1.0f - fabsf(fmodf(h6, 2.0f) - 1.0f));
        float m = brightness - c;

        float rf, gf, bf;
        if      (h6 < 1) { rf = c; gf = x; bf = 0; }
        else if (h6 < 2) { rf = x; gf = c; bf = 0; }
        else if (h6 < 3) { rf = 0; gf = c; bf = x; }
        else if (h6 < 4) { rf = 0; gf = x; bf = c; }
        else if (h6 < 5) { rf = x; gf = 0; bf = c; }
        else              { rf = c; gf = 0; bf = x; }

        int r = (int)((rf + m) * 255);
        int g = (int)((gf + m) * 255);
        int b = (int)((bf + m) * 255);
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;

        /* Line width: thinner far, thicker near */
        int lw = (t > 0.5f) ? 3 : 2;

        lv_draw_line_dsc_t line_dsc;
        lv_draw_line_dsc_init(&line_dsc);
        line_dsc.color = lv_color_make(r, g, b);
        line_dsc.width = lw;
        line_dsc.p1.x = (int)proj[v0][0];
        line_dsc.p1.y = (int)proj[v0][1];
        line_dsc.p2.x = (int)proj[v1][0];
        line_dsc.p2.y = (int)proj[v1][1];
        lv_draw_line(&layer, &line_dsc);
    }

    lv_canvas_finish_layer(canvas, &layer);
}

/* ── Animation timer — runs at ~30fps ── */

static void anim_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    bool needs_render = false;

    /* Apply spin inertia in screen space */
    if (fabsf(spin_vx) > SPIN_STOP_THRESHOLD || fabsf(spin_vy) > SPIN_STOP_THRESHOLD) {
        apply_screen_rotation(spin_vx, spin_vy);
        spin_vx *= SPIN_DECAY;
        spin_vy *= SPIN_DECAY;
        if (fabsf(spin_vx) <= SPIN_STOP_THRESHOLD) spin_vx = 0;
        if (fabsf(spin_vy) <= SPIN_STOP_THRESHOLD) spin_vy = 0;
        needs_render = true;
    }

    /* Decay burst */
    if (burst_scale > 0.01f || burst_brightness > 0.01f) {
        burst_scale *= BURST_DECAY;
        burst_brightness *= BURST_DECAY;
        if (burst_scale < 0.01f) burst_scale = 0;
        if (burst_brightness < 0.01f) burst_brightness = 0;
        needs_render = true;
    }

    if (needs_render) {
        render_icosahedron();
    }
}

/* ── Gesture text + stats ── */

static lv_timer_t *gesture_clear_timer = NULL;

static void gesture_clear_cb(lv_timer_t *timer)
{
    (void)timer;
    if (gesture_label) lv_label_set_text(gesture_label, "");
    gesture_clear_timer = NULL;
}

static void show_gesture_text(const char *text, uint32_t color)
{
    if (!gesture_label) return;
    lv_label_set_text(gesture_label, text);
    lv_obj_set_style_text_color(gesture_label, lv_color_hex(color), 0);

    if (gesture_clear_timer) {
        lv_timer_reset(gesture_clear_timer);
    } else {
        gesture_clear_timer = lv_timer_create(gesture_clear_cb, 1500, NULL);
        lv_timer_set_repeat_count(gesture_clear_timer, 1);
    }
}

static void update_stats_label(void)
{
    if (!stats_label || swipe_count == 0) return;

    char buf[128];
    if (swipe_count == 1) {
        snprintf(buf, sizeof(buf), "fastest swipe: %"PRId32"ms",
                 best_duration_ms);
    } else {
        snprintf(buf, sizeof(buf),
                 "fastest swipe: %"PRId32"ms\n"
                 "fastest consecutive: %"PRId32"ms",
                 best_duration_ms, best_gap_ms);
    }
    lv_label_set_text(stats_label, buf);
}

/* ── Swipe handling ── */

static void handle_swipe(const char *arrow, const char *dir_name)
{
    int64_t now_us = esp_timer_get_time();

    int32_t duration_ms = (press_start_us > 0)
                              ? (int32_t)((now_us - press_start_us) / 1000) : 0;
    if (duration_ms < best_duration_ms) best_duration_ms = duration_ms;

    int32_t gap_ms = 0;
    if (last_swipe_us > 0) {
        gap_ms = (int32_t)((now_us - last_swipe_us) / 1000);
        if (swipe_count > 0 && gap_ms < best_gap_ms) best_gap_ms = gap_ms;
    }
    last_swipe_us = now_us;
    swipe_count++;

    /* Get actual swipe vector from press point to current point */
    lv_point_t cur = {0, 0};
    lv_indev_t *indev = lv_indev_active();
    if (indev) lv_indev_get_point(indev, &cur);

    float dx = (float)(cur.x - press_point.x);
    float dy = (float)(cur.y - press_point.y);
    float dist = sqrtf(dx * dx + dy * dy);

    /* Spin speed from swipe velocity (distance / time) */
    float speed = 0.15f;
    if (duration_ms > 0 && dist > 5.0f) {
        speed = dist / (float)duration_ms * 0.5f;
        if (speed < 0.05f) speed = 0.05f;
        if (speed > 0.5f) speed = 0.5f;
    }

    /* Spin axis perpendicular to the actual swipe vector.
     * Pushing the front face: swipe (dx,dy) → spin around (-dy, -dx).
     * Normalize so diagonal swipes have correct magnitude. */
    if (dist > 1.0f) {
        spin_vx = -dy / dist * speed;
        spin_vy =  dx / dist * speed;
    }

    ESP_LOGI(TAG, "SWIPE: %s  dur=%"PRId32"ms  gap=%"PRId32"ms  speed=%.2f  vec=(%.0f,%.0f)  n=%"PRIu32,
             dir_name, duration_ms, gap_ms, (double)speed, (double)dx, (double)dy, swipe_count);

    char buf[48];
    snprintf(buf, sizeof(buf), "%s %s  %"PRId32"ms", arrow, dir_name, duration_ms);
    show_gesture_text(buf, 0x00AAFF);
    update_stats_label();
    gesture_fired = true;
}

/* ── Touch coordinate + strength display ── */

static void update_coord_label(lv_coord_t x, lv_coord_t y)
{
    if (!coord_label) return;

    esp_lcd_touch_handle_t th = board_get_touch_handle();
    uint16_t strength = 0;
    if (th) {
        esp_lcd_touch_point_data_t point;
        uint8_t n = 0;
        if (esp_lcd_touch_get_data(th, &point, &n, 1) == ESP_OK && n > 0) {
            strength = point.strength;
        }
    }

    char buf[80];
    if (strength > 0) {
        snprintf(buf, sizeof(buf), "(%ld, %ld)  str: %u", (long)x, (long)y, strength);
    } else {
        snprintf(buf, sizeof(buf), "(%ld, %ld)", (long)x, (long)y);
    }
    lv_label_set_text(coord_label, buf);
}

/* ── Screen-level gesture handler ── */

static void screen_gesture_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;

#if LV_USE_GESTURE_RECOGNITION
    lv_indev_gesture_type_t gtype = lv_event_get_gesture_type(e);

    if (gtype == LV_INDEV_GESTURE_PINCH) {
        /* Only process if the pinch is actively recognized — prevents
         * stale pinch state from capturing subsequent single-finger swipes */
        lv_indev_gesture_state_t state = lv_event_get_gesture_state(e, LV_INDEV_GESTURE_PINCH);
        if (state != LV_INDEV_GESTURE_STATE_RECOGNIZED &&
            state != LV_INDEV_GESTURE_STATE_ONGOING) {
            goto try_swipe;
        }

        float scale = lv_event_get_pinch_scale(e);
        /* Ignore near-unity scale — likely a misdetection */
        if (fabsf(scale - 1.0f) < 0.05f) goto try_swipe;

        zoom = scale;
        if (zoom < 0.3f) zoom = 0.3f;
        if (zoom > 4.0f) zoom = 4.0f;
        render_icosahedron();

        char buf[48];
        if (scale >= 1.0f) {
            snprintf(buf, sizeof(buf), "ZOOM IN  x%.2f", (double)scale);
            show_gesture_text(buf, 0x00DD00);
        } else {
            snprintf(buf, sizeof(buf), "ZOOM OUT  x%.2f", (double)scale);
            show_gesture_text(buf, 0xDD0000);
        }
        ESP_LOGI(TAG, "PINCH: scale=%.2f  state=%d", (double)scale, state);
        gesture_fired = true;
        return;
    }

    /* Two-finger swipe and rotation disabled — not common touch UX,
     * and rotation consumes touches that should go to pinch */

try_swipe:
#endif

    /* Single-finger swipe */
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    switch (dir) {
        case LV_DIR_LEFT:  handle_swipe(LV_SYMBOL_LEFT,  "LEFT");  break;
        case LV_DIR_RIGHT: handle_swipe(LV_SYMBOL_RIGHT, "RIGHT"); break;
        case LV_DIR_TOP:   handle_swipe(LV_SYMBOL_UP,    "UP");    break;
        case LV_DIR_BOTTOM:handle_swipe(LV_SYMBOL_DOWN,  "DOWN");  break;
        default: break;
    }
}

/* ── Screen-level touch event handler ── */

static void screen_press_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        press_start_us = esp_timer_get_time();
        in_drag_mode = false;
        gesture_fired = false;
        lv_indev_t *indev = lv_indev_active();
        if (indev) lv_indev_get_point(indev, &press_point);

        /* Subtle color shift on contact — not the full burst */
        burst_brightness = 0.2f;
        render_icosahedron();
    }

    /* Tap release: only if no swipe, drag, or pinch happened this touch */
    if (code == LV_EVENT_SHORT_CLICKED && !gesture_fired) {
        burst_scale = 0.3f;
        burst_brightness = 0.6f;
        render_icosahedron();
        show_gesture_text("TAP", 0xFFFFFF);
        ESP_LOGI(TAG, "TAP");
    }

    /* Drag-to-roll: slow initial movement enters drag mode,
     * then tracks freely at any speed until finger lifts */
    if (code == LV_EVENT_PRESSING) {
        lv_indev_t *indev = lv_indev_active();
        if (indev) {
            lv_point_t vect;
            lv_indev_get_vect(indev, &vect);
            if (vect.x != 0 || vect.y != 0) {
                float vmag = sqrtf((float)(vect.x * vect.x + vect.y * vect.y));
                if (!in_drag_mode) {
                    /* Enter drag mode only with slow, deliberate movement */
                    if (vmag < 8.0f) {
                        in_drag_mode = true;
                    }
                }
                if (in_drag_mode) {
                    gesture_fired = true;
                    float sensitivity = 0.008f;
                    apply_screen_rotation(-(float)vect.y * sensitivity,
                                           (float)vect.x * sensitivity);
                    spin_vx = 0;
                    spin_vy = 0;
                    render_icosahedron();
                }
            }
        }
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        in_drag_mode = false;
    }

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        lv_indev_t *indev = lv_indev_active();
        if (indev) {
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            update_coord_label(p.x, p.y);
        }
    }
}

/* ── Button event handler ── */

static void btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    quadrant_t *q = (quadrant_t *)lv_event_get_user_data(e);

    if (code == LV_EVENT_PRESSED) {
        lv_obj_set_style_bg_color(q->btn, COLOR_ACTIVE, 0);
        ESP_LOGI(TAG, "PRESSED: %s", q->name);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        lv_obj_set_style_bg_color(q->btn, COLOR_IDLE, 0);
    }
}

/* ── UI creation ── */

static void create_ui(lv_display_t *disp)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    int w = lv_display_get_horizontal_resolution(disp);
    int h = lv_display_get_vertical_resolution(disp);
    int gap = 4;

    /* ── Small corner buttons pinned to actual corners ── */
    const char *names[] = { "TL", "TR", "BL", "BR" };
    int x_pos[] = { gap, w - CORNER_BTN_SIZE - gap,
                    gap, w - CORNER_BTN_SIZE - gap };
    int y_pos[] = { gap, gap,
                    h - CORNER_BTN_SIZE - gap, h - CORNER_BTN_SIZE - gap };

    for (int i = 0; i < NUM_QUADRANTS; i++) {
        quadrant_t *q = &quadrants[i];
        q->name = names[i];

        q->btn = lv_obj_create(scr);
        lv_obj_remove_style_all(q->btn);
        lv_obj_set_size(q->btn, CORNER_BTN_SIZE, CORNER_BTN_SIZE);
        lv_obj_set_pos(q->btn, x_pos[i], y_pos[i]);
        lv_obj_set_style_bg_opa(q->btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(q->btn, COLOR_IDLE, 0);
        lv_obj_set_style_radius(q->btn, 8, 0);

        q->label = lv_label_create(q->btn);
        lv_label_set_text(q->label, q->name);
        lv_obj_set_style_text_color(q->label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(q->label, &lv_font_montserrat_24, 0);
        lv_obj_center(q->label);

        lv_obj_add_event_cb(q->btn, btn_event_cb, LV_EVENT_ALL, q);
    }

    /* ── Canvas for 3D rendering — full screen ── */
    canvas_w = w;
    canvas_h = h;
    canvas_cx = canvas_w / 2;
    canvas_cy = canvas_h / 2;

    canvas = lv_canvas_create(scr);
    lv_obj_set_pos(canvas, 0, 0);

    /* Allocate canvas buffer in PSRAM */
    size_t buf_size = canvas_w * canvas_h * sizeof(lv_color_t);
    void *buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate canvas buffer (%d bytes)", (int)buf_size);
        return;
    }
    lv_canvas_set_buffer(canvas, buf, canvas_w, canvas_h, LV_COLOR_FORMAT_NATIVE);
    lv_canvas_fill_bg(canvas, lv_color_hex(0x000000), LV_OPA_COVER);

    /* Canvas behind buttons */
    lv_obj_move_to_index(canvas, 0);

    /* Apply initial tilt and render */
    if (!rot_mat_initialized) {
        apply_screen_rotation(0.3f, 0.5f);
        rot_mat_initialized = true;
    }
    render_icosahedron();

    /* ── Gesture text label ── */
    gesture_label = lv_label_create(scr);
    lv_obj_set_width(gesture_label, w);
    lv_obj_set_style_text_color(gesture_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(gesture_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(gesture_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(gesture_label, 0, CORNER_BTN_SIZE + gap + 4);
    lv_label_set_text(gesture_label, "");

    /* ── Stats label — above bottom buttons, below icosahedron center zone ── */
    int bottom_btn_top = h - CORNER_BTN_SIZE - gap;
    int stats_y = bottom_btn_top - 60;  /* 60px = ~2 lines of montserrat_24 + margin */
    int shape_bottom = canvas_cy + (int)((float)canvas_w * 0.18f * PHI) + 8;
    if (stats_y < shape_bottom) {
        stats_y = shape_bottom;
    }
    stats_label = lv_label_create(scr);
    lv_obj_set_width(stats_label, w);
    lv_obj_set_style_text_color(stats_label, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(stats_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(stats_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(stats_label, 0, stats_y);
    lv_label_set_text(stats_label, "");

    /* ── Coordinate readout — between bottom corner buttons ── */
    coord_label = lv_label_create(scr);
    lv_obj_set_width(coord_label, w - CORNER_BTN_SIZE * 2 - gap * 4);
    lv_obj_set_style_text_color(coord_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(coord_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(coord_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(coord_label, CORNER_BTN_SIZE + gap * 2,
                   h - CORNER_BTN_SIZE / 2 - 12);
    lv_label_set_text(coord_label, "");

    /* ── Screen-level event handlers ── */
    lv_obj_add_event_cb(scr, screen_gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(scr, screen_press_cb, LV_EVENT_ALL, NULL);

    /* ── Animation timer (~30fps) ── */
    lv_timer_create(anim_timer_cb, 33, NULL);
}

/* ── Main ── */

void app_main(void)
{
    ESP_LOGI(TAG, "Touch test starting");
    ESP_LOGI(TAG, "Display: %dx%d", BOARD_LCD_H_RES, BOARD_LCD_V_RES);

    lv_display_t *disp;
    lv_indev_t *touch;
    board_app_config_t app_cfg = { .landscape = false };
    board_init(&app_cfg, &disp, &touch);

    if (!touch) {
        ESP_LOGE(TAG, "Touch init failed — check serial log for driver errors");
    }

#if LV_USE_GESTURE_RECOGNITION
    if (touch) {
        /* Lower pinch thresholds — defaults (1.5x / 0.75x) are too hard to trigger */
        lv_indev_set_pinch_up_threshold(touch, 1.1f);
        lv_indev_set_pinch_down_threshold(touch, 0.9f);
        /* Disable rotation recognizer — it consumes two-finger touches
         * before the swipe recognizer can process them */
        lv_indev_set_rotation_rad_threshold(touch, 999.0f);
    }
#endif

    if (lvgl_port_lock(0)) {
        create_ui(disp);
        lvgl_port_unlock();
    }

    board_backlight_set(100);

    ESP_LOGI(TAG, "Touch test running");
    board_run();
}
