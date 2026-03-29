/* display.c */
#include "display.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <zephyr/input/input.h>
#include <lvgl.h>
#include <stdio.h>
#include "../states/states.h"

/* Pivot sits left-center of the animation area */
#define ANIM_PIVOT_X   80
#define ANIM_PIVOT_Y   150
#define ANIM_BAR_LEN   60
#define ANIM_LAYER_GAP 6    /* px offset between bottom (wood) and top (acrylic) layers */

/* ── Display device ── */
static const struct device *display_dev;

/* ── Process screen labels ── */
static lv_obj_t *process_value_label;
static lv_obj_t *process_time_label;
static lv_obj_t *process_pct_label;

/* ── UI elements ── */
static lv_obj_t *position_label;
static lv_obj_t *direction_label;
static lv_obj_t *state_label;
static lv_obj_t *bar;

/* ── Animation elements ──
 * Each bar has two layers: bottom (wood/grey) and top (acrylic/white).
 * fixed  = right-extending bar  (wood bottom + acrylic top)
 * moving = left-extending bar   (wood bottom + acrylic top, rotates up)
 */
static lv_obj_t            *anim_fixed_bot;       /* grey  layer, fixed bar  */
static lv_obj_t            *anim_fixed_top;       /* white layer, fixed bar  */
static lv_obj_t            *anim_moving_bot;      /* grey  layer, moving bar */
static lv_obj_t            *anim_moving_top;      /* white layer, moving bar */
static lv_obj_t            *anim_value_label;
static lv_point_precise_t   anim_fixed_bot_pts[2];
static lv_point_precise_t   anim_fixed_top_pts[2];
static lv_point_precise_t   anim_moving_bot_pts[2];
static lv_point_precise_t   anim_moving_top_pts[2];
static int                  auto_angle = 0;
static int                  auto_dir   = 1;

/* ── Input selection state ── */
typedef enum {
    INPUT_BEND_ANGLE = 0,
    INPUT_THICKNESS,
    INPUT_BEND_RADII,
    INPUT_COUNT
} input_step_t;

static input_step_t current_input_step = INPUT_BEND_ANGLE;
static int          sel_bend_angle     = 0;
static int          sel_thickness      = 0;
static int          sel_bend_radii     = 0;
static lv_obj_t    *input_value_label  = NULL;

/* ── Encoder state ── */
static int position  = 0;
static int direction = 0;
static K_MUTEX_DEFINE(encoder_mutex);

/* ── LVGL styles ── */
static lv_style_t style_screen;
static lv_style_t style_title;
static lv_style_t style_subtitle;
static lv_style_t style_body;
static lv_style_t style_note;
static lv_style_t style_line_acrylic;   /* white top layer  */
static lv_style_t style_line_wood;      /* grey  bot layer  */
static bool styles_initialized = false;
static bool line_styles_init   = false;

/* ── sin lookup table (scaled to 1000) for 0..90 degrees ── */
static const int16_t sin_lut[91] = {
       0,  17,  35,  52,  70,  87, 105, 122, 139, 156,
     174, 191, 208, 225, 242, 259, 276, 292, 309, 326,
     342, 358, 375, 391, 407, 423, 438, 454, 469, 485,
     500, 515, 530, 545, 559, 574, 588, 602, 616, 629,
     643, 656, 669, 682, 695, 707, 719, 731, 743, 755,
     766, 777, 788, 799, 809, 819, 829, 839, 848, 857,
     866, 874, 882, 891, 899, 906, 914, 921, 927, 934,
     940, 946, 951, 956, 961, 966, 970, 974, 978, 982,
     985, 988, 990, 993, 995, 996, 998, 999, 999,1000,
    1000
};
#define COS_LUT(a) sin_lut[90 - (a)]

/* ══════════════════════════════════════════════════════════════
 *  Timer + exit — declared early so clear_screen can use them
 * ══════════════════════════════════════════════════════════════ */

static void bend_anim_set_angle(int angle_deg);   /* forward declaration */

static void bend_anim_timer_cb(struct k_timer *t)
{
    ARG_UNUSED(t);
    auto_angle += auto_dir;
    if (auto_angle >= 90) { auto_angle = 90; auto_dir = -1; }
    if (auto_angle <= 0)  { auto_angle = 0;  auto_dir  =  1; }
    bend_anim_set_angle(auto_angle);
}
K_TIMER_DEFINE(bend_anim_timer, bend_anim_timer_cb, NULL);

static void bend_angle_input_screen_exit(void)
{
    k_timer_stop(&bend_anim_timer);
    anim_fixed_bot   = NULL;
    anim_fixed_top   = NULL;
    anim_moving_bot  = NULL;
    anim_moving_top  = NULL;
    anim_value_label = NULL;
}

/* ══════════════════════════════════════════════════════════════
 *  Internal helpers
 * ══════════════════════════════════════════════════════════════ */

static void init_styles(void)
{
    if (styles_initialized) return;

    lv_style_init(&style_screen);
    lv_style_set_bg_color(&style_screen, lv_color_black());
    lv_style_set_bg_opa(&style_screen, LV_OPA_COVER);

    lv_style_init(&style_title);
    lv_style_set_text_color(&style_title, lv_color_white());
    lv_style_set_text_font(&style_title, &lv_font_montserrat_24);

    lv_style_init(&style_subtitle);
    lv_style_set_text_color(&style_subtitle, lv_color_white());
    lv_style_set_text_font(&style_subtitle, &lv_font_montserrat_18);

    lv_style_init(&style_body);
    lv_style_set_text_color(&style_body, lv_color_white());
    lv_style_set_text_font(&style_body, &lv_font_montserrat_12);

    lv_style_init(&style_note);
    lv_style_set_text_color(&style_note, lv_color_hex(0xAAAAAA));
    lv_style_set_text_font(&style_note, &lv_font_montserrat_12);

    styles_initialized = true;
}

static void init_line_styles(void)
{
    if (line_styles_init) return;

    /* Bottom layer — grey wood */
    lv_style_init(&style_line_wood);
    lv_style_set_line_color(&style_line_wood, lv_color_hex(0x888888));
    lv_style_set_line_width(&style_line_wood, 8);
    lv_style_set_line_rounded(&style_line_wood, true);

    /* Top layer — white acrylic, slightly thinner so grey peeks below */
    lv_style_init(&style_line_acrylic);
    lv_style_set_line_color(&style_line_acrylic, lv_color_white());
    lv_style_set_line_width(&style_line_acrylic, 4);
    lv_style_set_line_rounded(&style_line_acrylic, true);

    line_styles_init = true;
}

static void clear_screen(void)
{
    bend_angle_input_screen_exit();
    bar                 = NULL;
    input_value_label   = NULL;
    process_value_label = NULL;
    process_time_label  = NULL;
    process_pct_label   = NULL;
    lv_obj_clean(lv_scr_act());
    lv_obj_add_style(lv_scr_act(), &style_screen, 0);
}

static lv_obj_t *make_label(lv_obj_t *parent,
                             lv_style_t *style,
                             const char *text,
                             lv_align_t align,
                             lv_coord_t x_ofs,
                             lv_coord_t y_ofs)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_add_style(lbl, style, 0);
    lv_obj_align(lbl, align, x_ofs, y_ofs);
    return lbl;
}

/* ── Encoder input callback ── */
static void encoder_cb(struct input_event *evt, void *user_data)
{
    ARG_UNUSED(user_data);
    if (evt->type == INPUT_EV_REL && evt->code == INPUT_REL_X) {
        k_mutex_lock(&encoder_mutex, K_FOREVER);
        if (evt->value > 0) {
            position = MIN(position + 1, 100);
            direction = 1;
        } else {
            position = MAX(position - 1, 0);
            direction = -1;
        }
        k_mutex_unlock(&encoder_mutex);
        printk("Encoder position: %d direction: %d\n", position, direction);
    }
}
INPUT_CALLBACK_DEFINE(NULL, encoder_cb, NULL);

/* ══════════════════════════════════════════════════════════════
 *  Public encoder API
 * ══════════════════════════════════════════════════════════════ */

int encoder_get_position(void) { return position; }

int encoder_get_direction(void)
{
    k_mutex_lock(&encoder_mutex, K_FOREVER);
    int dir = direction;
    direction = 0;
    k_mutex_unlock(&encoder_mutex);
    return dir;
}

/* ══════════════════════════════════════════════════════════════
 *  Bend animation
 * ══════════════════════════════════════════════════════════════ */

static void bend_anim_set_angle(int angle_deg)
{
    if (angle_deg < 0)  angle_deg = 0;
    if (angle_deg > 90) angle_deg = 90;

    int dx = (ANIM_BAR_LEN * COS_LUT(angle_deg)) / 1000;
    int dy = (ANIM_BAR_LEN * sin_lut[angle_deg]) / 1000;

    /* Fixed bar — always horizontal, extends RIGHT from pivot
     * bottom layer sits below top layer by ANIM_LAYER_GAP px */
    anim_fixed_bot_pts[0].x = ANIM_PIVOT_X;
    anim_fixed_bot_pts[0].y = ANIM_PIVOT_Y + ANIM_LAYER_GAP;
    anim_fixed_bot_pts[1].x = ANIM_PIVOT_X + ANIM_BAR_LEN;
    anim_fixed_bot_pts[1].y = ANIM_PIVOT_Y + ANIM_LAYER_GAP;

    anim_fixed_top_pts[0].x = ANIM_PIVOT_X;
    anim_fixed_top_pts[0].y = ANIM_PIVOT_Y;
    anim_fixed_top_pts[1].x = ANIM_PIVOT_X + ANIM_BAR_LEN;
    anim_fixed_top_pts[1].y = ANIM_PIVOT_Y;

    /* Moving bar — extends LEFT from pivot, rotates UP
     * perpendicular offset for the two layers uses the bar's normal vector:
     *   normal to (dx, -dy) is (dy, dx) — shift bottom layer along normal */
    int nx = (ANIM_LAYER_GAP * sin_lut[angle_deg]) / 1000;
    int ny = (ANIM_LAYER_GAP * COS_LUT(angle_deg)) / 1000;

    anim_moving_bot_pts[0].x = ANIM_PIVOT_X      + nx;
    anim_moving_bot_pts[0].y = ANIM_PIVOT_Y      + ny;
    anim_moving_bot_pts[1].x = ANIM_PIVOT_X - dx + nx;
    anim_moving_bot_pts[1].y = ANIM_PIVOT_Y - dy + ny;

    anim_moving_top_pts[0].x = ANIM_PIVOT_X;
    anim_moving_top_pts[0].y = ANIM_PIVOT_Y;
    anim_moving_top_pts[1].x = ANIM_PIVOT_X - dx;
    anim_moving_top_pts[1].y = ANIM_PIVOT_Y - dy;

    if (anim_fixed_bot  != NULL) lv_line_set_points(anim_fixed_bot,  anim_fixed_bot_pts,  2);
    if (anim_fixed_top  != NULL) lv_line_set_points(anim_fixed_top,  anim_fixed_top_pts,  2);
    if (anim_moving_bot != NULL) lv_line_set_points(anim_moving_bot, anim_moving_bot_pts, 2);
    if (anim_moving_top != NULL) lv_line_set_points(anim_moving_top, anim_moving_top_pts, 2);

    /* Display angle = 180 - internal angle
     * (internal 0 = flat/180°, internal 90 = L-shape/90°) */
    if (anim_value_label != NULL) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d deg", 180 - angle_deg);
        lv_label_set_text(anim_value_label, buf);
    }
}

static void bend_anim_create(lv_obj_t *parent)
{
    init_line_styles();

    /* Fixed bar — draw bottom (grey) first, then top (white) over it */
    anim_fixed_bot_pts[0].x = ANIM_PIVOT_X;
    anim_fixed_bot_pts[0].y = ANIM_PIVOT_Y + ANIM_LAYER_GAP;
    anim_fixed_bot_pts[1].x = ANIM_PIVOT_X + ANIM_BAR_LEN;
    anim_fixed_bot_pts[1].y = ANIM_PIVOT_Y + ANIM_LAYER_GAP;

    anim_fixed_bot = lv_line_create(parent);
    lv_line_set_points(anim_fixed_bot, anim_fixed_bot_pts, 2);
    lv_obj_add_style(anim_fixed_bot, &style_line_wood, 0);

    anim_fixed_top_pts[0].x = ANIM_PIVOT_X;
    anim_fixed_top_pts[0].y = ANIM_PIVOT_Y;
    anim_fixed_top_pts[1].x = ANIM_PIVOT_X + ANIM_BAR_LEN;
    anim_fixed_top_pts[1].y = ANIM_PIVOT_Y;

    anim_fixed_top = lv_line_create(parent);
    lv_line_set_points(anim_fixed_top, anim_fixed_top_pts, 2);
    lv_obj_add_style(anim_fixed_top, &style_line_acrylic, 0);

    /* Moving bar — starts flat (pointing left), draw bottom then top */
    anim_moving_bot_pts[0].x = ANIM_PIVOT_X;
    anim_moving_bot_pts[0].y = ANIM_PIVOT_Y + ANIM_LAYER_GAP;
    anim_moving_bot_pts[1].x = ANIM_PIVOT_X - ANIM_BAR_LEN;
    anim_moving_bot_pts[1].y = ANIM_PIVOT_Y + ANIM_LAYER_GAP;

    anim_moving_bot = lv_line_create(parent);
    lv_line_set_points(anim_moving_bot, anim_moving_bot_pts, 2);
    lv_obj_add_style(anim_moving_bot, &style_line_wood, 0);

    anim_moving_top_pts[0].x = ANIM_PIVOT_X;
    anim_moving_top_pts[0].y = ANIM_PIVOT_Y;
    anim_moving_top_pts[1].x = ANIM_PIVOT_X - ANIM_BAR_LEN;
    anim_moving_top_pts[1].y = ANIM_PIVOT_Y;

    anim_moving_top = lv_line_create(parent);
    lv_line_set_points(anim_moving_top, anim_moving_top_pts, 2);
    lv_obj_add_style(anim_moving_top, &style_line_acrylic, 0);

    /* Value label — right side */
    anim_value_label = lv_label_create(parent);
    lv_label_set_text(anim_value_label, "180 deg");
    lv_obj_add_style(anim_value_label, &style_title, 0);
    lv_obj_align(anim_value_label, LV_ALIGN_CENTER, 60, 20);
}

/* ══════════════════════════════════════════════════════════════
 *  Display init
 * ══════════════════════════════════════════════════════════════ */

int display_init(void)
{
    display_dev = DEVICE_DT_GET(DT_NODELABEL(ili9341));
    if (!device_is_ready(display_dev)) {
        printk("Display not ready\n");
        return -1;
    }
    display_blanking_off(display_dev);

    struct display_capabilities caps;
    display_get_capabilities(display_dev, &caps);
    printk("Display ready: %dx%d\n", caps.x_resolution, caps.y_resolution);

    const struct device *enc_dev = DEVICE_DT_GET(DT_NODELABEL(encoder));
    printk("Encoder %s\n", device_is_ready(enc_dev) ? "ready" : "NOT ready");

    init_styles();
    return 0;
}

/* ══════════════════════════════════════════════════════════════
 *  Screens
 * ══════════════════════════════════════════════════════════════ */

/* SCREEN 1 — DIRECTIONS (STATE_IDLE) */
static void direction_screen(void)
{
    printk("display: directions screen\n");
    clear_screen();

    make_label(lv_scr_act(), &style_subtitle, "Directions:",
               LV_ALIGN_TOP_LEFT, 30, 20);

    lv_obj_t *steps = lv_label_create(lv_scr_act());
    lv_label_set_long_mode(steps, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(steps, lv_pct(90));
    lv_obj_set_height(steps, LV_SIZE_CONTENT);
    lv_label_set_text(steps,
        "1. Insert acrylic on left hand side and align to desired position.\n\n"
        "2. Clamp by turning upper clamp knob (1).\n\n"
        "3. Manually adjust knob (2) to desired bend radius and tighten nuts to lock.\n\n"
        "4. Hit " LV_SYMBOL_PLAY " to proceed and select inputs.");
    lv_obj_add_style(steps, &style_body, 0);
    lv_obj_align(steps, LV_ALIGN_TOP_LEFT, 30, 48);

    lv_obj_t *estop = lv_label_create(lv_scr_act());
    lv_label_set_long_mode(estop, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(estop, lv_pct(100));
    lv_label_set_text(estop,
        "* E-stop on right side of machine for emergency");
    lv_obj_add_style(estop, &style_note, 0);
    lv_obj_align(estop, LV_ALIGN_BOTTOM_LEFT, 13, -18);
}

/* SCREEN 2 — INPUT SELECTION (STATE_INITIALIZATION) */

/* Bend angle sub-screen with animation */
static void bend_angle_input_screen(void)
{
    clear_screen();

    make_label(lv_scr_act(), &style_note, "1 / 3",
               LV_ALIGN_TOP_RIGHT, -10, 10);
    make_label(lv_scr_act(), &style_title, "Bend Angle",
               LV_ALIGN_CENTER, 60, -25);

    bend_anim_create(lv_scr_act());

    make_label(lv_scr_act(), &style_body,
               "Hit " LV_SYMBOL_PLAY " to proceed\n"
               "Hit " LV_SYMBOL_STOP " to return",
               LV_ALIGN_BOTTOM_MID, 60, 5);

    /* TODO: when encoder is ready, remove timer and instead call:
     *   input_selection_update_value(delta) from encoder_cb
     *   bend_anim_set_angle(sel_bend_angle)
     */
    auto_angle = 0;
    auto_dir   = 1;
    k_timer_start(&bend_anim_timer, K_SECONDS(1), K_SECONDS(1));
}

/* Generic input sub-screen (thickness, bend radii) */
static void input_screen(const char *title, const char *unit, int value)
{
    clear_screen();

    char step_buf[8];
    snprintf(step_buf, sizeof(step_buf), "%d / %d",
             (int)current_input_step + 1, (int)INPUT_COUNT);
    make_label(lv_scr_act(), &style_note, step_buf,
               LV_ALIGN_TOP_RIGHT, -10, 10);

    make_label(lv_scr_act(), &style_title, title,
               LV_ALIGN_CENTER, 60, -25);

    char val_buf[16];
    snprintf(val_buf, sizeof(val_buf), "%d %s", value, unit);
    input_value_label = make_label(lv_scr_act(), &style_title, val_buf,
                                   LV_ALIGN_CENTER, 60, 5);

    /* TODO: image/animation placeholder — add here when ready */

    make_label(lv_scr_act(), &style_subtitle,
               "Hit " LV_SYMBOL_PLAY " to proceed\n"
               "Hit " LV_SYMBOL_STOP " to return",
               LV_ALIGN_BOTTOM_MID, 0, -5);
}

static void input_selection_show_step(void)
{
    bend_angle_input_screen_exit();
    switch (current_input_step) {
        case INPUT_BEND_ANGLE: bend_angle_input_screen();                         break;
        case INPUT_THICKNESS:  input_screen("Thickness",  "mm", sel_thickness);   break;
        case INPUT_BEND_RADII: input_screen("Bend Radii", "mm", sel_bend_radii);  break;
        default: break;
    }
}

static void input_selection_screen(void)
{
    printk("display: input selection screen\n");
    current_input_step = INPUT_BEND_ANGLE;
    sel_bend_angle = 0;
    sel_thickness  = 0;
    sel_bend_radii = 0;
    input_selection_show_step();
}

void input_selection_next(void)
{
    if (current_input_step < INPUT_COUNT - 1) {
        current_input_step++;
        input_selection_show_step();
    } else {
        g_inputs.bend_angle = sel_bend_angle;
        g_inputs.thickness  = sel_thickness;
        g_inputs.bend_radii = sel_bend_radii;
        printk("Inputs confirmed: angle=%d thickness=%d radii=%d\n",
               g_inputs.bend_angle, g_inputs.thickness, g_inputs.bend_radii);
        sm_transition(STATE_BEND);
    }
}

void input_selection_prev(void)
{
    if (current_input_step > INPUT_BEND_ANGLE) {
        current_input_step--;
        input_selection_show_step();
    } else {
        sm_transition(STATE_IDLE);
    }
}

void input_selection_update_value(int delta)
{
    switch (current_input_step) {
        case INPUT_BEND_ANGLE:
            sel_bend_angle = CLAMP(sel_bend_angle + delta, 0, 90);
            /* TODO: also call bend_anim_set_angle(90 - sel_bend_angle) here */
            break;
        case INPUT_THICKNESS:
            sel_thickness = CLAMP(sel_thickness + delta, 0, 50);
            break;
        case INPUT_BEND_RADII:
            sel_bend_radii = CLAMP(sel_bend_radii + delta, 0, 100);
            break;
        default:
            break;
    }

    if (input_value_label != NULL) {
        char val_buf[16];
        const char *unit = (current_input_step == INPUT_BEND_ANGLE) ? "deg" : "mm";
        int val = (current_input_step == INPUT_BEND_ANGLE) ? sel_bend_angle :
                  (current_input_step == INPUT_THICKNESS)  ? sel_thickness  :
                                                             sel_bend_radii;
        snprintf(val_buf, sizeof(val_buf), "%d %s", val, unit);
        lv_label_set_text(input_value_label, val_buf);
    }
}

/* SCREEN 3 — PROCESS (STATE_BEND / STATE_COOL) */
static void process_screen(const char *header, const char *value)
{
    printk("display: process screen (%s)\n", header);
    clear_screen();

    make_label(lv_scr_act(), &style_title, header, LV_ALIGN_CENTER, 0, -50);

    /* TODO: replace with display_update_value() when live data ready */
    process_value_label = make_label(lv_scr_act(), &style_title, value,
                                     LV_ALIGN_CENTER, 0, -15);

    /* TODO: replace with display_update_progress() when live data ready */
    process_time_label = make_label(lv_scr_act(), &style_subtitle,
                                    "time remaining 23:10",
                                    LV_ALIGN_CENTER, 0, 20);

    bar = lv_bar_create(lv_scr_act());
    lv_obj_set_size(bar, 200, 20);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 50);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 75, LV_ANIM_OFF);   /* TODO: replace with live pct */

    process_pct_label = make_label(lv_scr_act(), &style_body, "75%",
                                   LV_ALIGN_CENTER, 0, 75);
}

/* SCREEN 4 — COMPLETE (STATE_COMPLETE) */
static void complete_screen(void)
{
    printk("display: complete screen\n");
    clear_screen();
    make_label(lv_scr_act(), &style_title, "Done!",
               LV_ALIGN_CENTER, 0, -10);
    make_label(lv_scr_act(), &style_body, "Safe to remove.",
               LV_ALIGN_CENTER, 0, 20);
}

/* ── Testing screen ── */
void test_display(void)
{
    printk("display: test screen\n");
    clear_screen();

    state_label = make_label(lv_scr_act(), &style_title, "IDLE",
                             LV_ALIGN_TOP_MID, 10, 10);
    lv_obj_set_style_text_color(state_label, lv_color_hex(0x00FF00), LV_PART_MAIN);

    make_label(lv_scr_act(), &style_title, "Encoder Position",
               LV_ALIGN_TOP_MID, 0, 0);

    position_label = make_label(lv_scr_act(), &style_body, "0",
                                LV_ALIGN_CENTER, 0, -10);
    make_label(lv_scr_act(), &style_body, "Direction:",
               LV_ALIGN_BOTTOM_LEFT, 20, -10);
    direction_label = make_label(lv_scr_act(), &style_body, "---",
                                 LV_ALIGN_BOTTOM_RIGHT, -20, -10);

    bar = lv_bar_create(lv_scr_act());
    lv_obj_set_size(bar, 200, 20);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 30);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
}

/* ══════════════════════════════════════════════════════════════
 *  Public screen API
 * ══════════════════════════════════════════════════════════════ */

void display_create_home_screen(void) { direction_screen(); }

void display_set_state(int state)
{
    switch ((system_state_t)state) {
        case STATE_IDLE:           direction_screen();                          break;
        case STATE_INITIALIZATION: input_selection_screen();                    break;
        case STATE_BEND:           process_screen("Bending...", "0\xC2\xB0");   break;
        case STATE_COOL:           process_screen("Cooling...", "");            break;
        case STATE_COMPLETE:       complete_screen();                           break;
        default: break;
    }
}

/* ══════════════════════════════════════════════════════════════
 *  Periodic update — called every 10 ms from display thread
 * ══════════════════════════════════════════════════════════════ */

void display_update(void)
{
    if (bar != NULL) {
        k_mutex_lock(&encoder_mutex, K_FOREVER);
        int pos = position;
        k_mutex_unlock(&encoder_mutex);
        lv_bar_set_value(bar, pos, LV_ANIM_ON);
    }
    lv_task_handler();
}

/* ══════════════════════════════════════════════════════════════
 *  TODO: uncomment when live data is ready
 *
 * void display_update_value(const char *value) {
 *     if (process_value_label != NULL)
 *         lv_label_set_text(process_value_label, value);
 * }
 * void display_update_progress(int pct, int min, int sec) {
 *     if (bar == NULL) return;
 *     char time_buf[32], pct_buf[8];
 *     snprintf(time_buf, sizeof(time_buf), "time remaining %02d:%02d", min, sec);
 *     snprintf(pct_buf,  sizeof(pct_buf),  "%d%%", pct);
 *     lv_bar_set_value(bar, pct, LV_ANIM_ON);
 *     if (process_time_label != NULL) lv_label_set_text(process_time_label, time_buf);
 *     if (process_pct_label  != NULL) lv_label_set_text(process_pct_label,  pct_buf);
 * }
 * Also add to display.h:
 *     void display_update_value(const char *value);
 *     void display_update_progress(int pct, int min, int sec);
 * ══════════════════════════════════════════════════════════════ */