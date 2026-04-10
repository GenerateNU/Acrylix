/* display.c */
#include "display.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <lvgl.h>
#include <stdio.h>

/* ── Bend animation geometry ── */
#define ANIM_PIVOT_X   80
#define ANIM_PIVOT_Y   150
#define ANIM_BAR_LEN   60
#define ANIM_LAYER_GAP 6

/* ── Thickness animation geometry ── */
#define THICK_X          20
#define THICK_BAR_LEN    130
#define THICK_BOT_Y      155
#define THICK_LAYER_GAP  5
/* Gap between wood and acrylic lines — visually represents acrylic thickness */
#define THICK_GAP_1_16   8   /* 1/16 mm */
#define THICK_GAP_1_8    18  /* 1/8 mm  (visually ~double) */

/* ── Display device ── */
static const struct device *display_dev;

/* ── Encoder state ── */
static int position  = 0;
static int direction = 0;
static K_MUTEX_DEFINE(encoder_mutex);

/* ── UI elements ── */
static lv_obj_t *bar;
static bool on_radius_screen = false;

/* ── Bend animation elements ── */
static lv_obj_t           *anim_fixed_bot;
static lv_obj_t           *anim_fixed_top;
static lv_obj_t           *anim_moving_bot;
static lv_obj_t           *anim_moving_top;
static lv_obj_t           *anim_value_label;
static lv_point_precise_t  anim_fixed_bot_pts[2];
static lv_point_precise_t  anim_fixed_top_pts[2];
static lv_point_precise_t  anim_moving_bot_pts[2];
static lv_point_precise_t  anim_moving_top_pts[2];
static int                 sel_bend_angle = 0;

/* ── Thickness animation elements ── */
static lv_obj_t           *thick_wood_bot;
static lv_obj_t           *thick_wood_top;
static lv_obj_t           *thick_acrylic_bot;
static lv_obj_t           *thick_acrylic_top;
static lv_obj_t           *thick_value_label;
static lv_point_t  thick_wood_bot_pts[2];
static lv_point_t  thick_wood_top_pts[2];
static lv_point_t  thick_acrylic_bot_pts[2];
static lv_point_t  thick_acrylic_top_pts[2];
/* 0 = 1/16 mm, 1 = 1/8 mm */
static int sel_thickness = 0;

/* ── LVGL styles ── */
static lv_style_t style_screen;
static lv_style_t style_title;
static lv_style_t style_subtitle;
static lv_style_t style_body;
static lv_style_t style_note;
static lv_style_t style_line_acrylic;
static lv_style_t style_line_wood;
static bool styles_initialized = false;
static bool line_styles_init   = false;

/* ── sin lookup table (scaled to 1000) for degrees ── */
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

static void bend_anim_set_angle(int angle_deg);   /* forward declaration */
static void thick_anim_set(int idx);              /* forward declaration */

K_TIMER_DEFINE(bend_anim_timer, NULL, NULL);

/* bend angle clear screen */
static void bend_angle_input_screen_exit(void)
{
    k_timer_stop(&bend_anim_timer);
    anim_fixed_bot   = NULL;
    anim_fixed_top   = NULL;
    anim_moving_bot  = NULL;
    anim_moving_top  = NULL;
    anim_value_label = NULL;
}

/* thickness clear screen */
static void thick_anim_clear(void)
{
    thick_wood_bot    = NULL;
    thick_wood_top    = NULL;
    thick_acrylic_bot = NULL;
    thick_acrylic_top = NULL;
    thick_value_label = NULL;
}

/* intialize UI styles */
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
    lv_style_set_text_font(&style_body, &lv_font_montserrat_14);

    lv_style_init(&style_note);
    lv_style_set_text_color(&style_note, lv_color_white());
    lv_style_set_text_font(&style_note, &lv_font_montserrat_12);

    styles_initialized = true;
}

static void init_line_styles(void)
{
    if (line_styles_init) return;

    lv_style_init(&style_line_wood);
    lv_style_set_line_color(&style_line_wood, lv_color_hex(0x888888));
    lv_style_set_line_width(&style_line_wood, 8);
    lv_style_set_line_rounded(&style_line_wood, true);

    lv_style_init(&style_line_acrylic);
    lv_style_set_line_color(&style_line_acrylic, lv_color_white());
    lv_style_set_line_width(&style_line_acrylic, 4);
    lv_style_set_line_rounded(&style_line_acrylic, true);

    line_styles_init = true;
}

/* clear screen function */
static void clear_screen(void)
{
    bend_angle_input_screen_exit();
    thick_anim_clear();
    bar = NULL;
    on_radius_screen = false;
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

/* ── Input callback (encoder + buttons) ── */
static void encoder_cb(struct input_event *evt, void *user_data)
{
    ARG_UNUSED(user_data);
    printk("input event: type=%d code=%d value=%d\n", evt->type, evt->code, evt->value);

    /* Forward button (PA8) */
    if (evt->type == INPUT_EV_KEY && evt->code == INPUT_KEY_ENTER) {
        if (evt->value == 1) {
            printk("Button: forward pressed\n");
            input_selection_next();
        }
        return;
    }

    /* Backward button (PA9) */
    if (evt->type == INPUT_EV_KEY && evt->code == INPUT_KEY_ESC) {
        if (evt->value == 1) {
            printk("Button: backward pressed\n");
            input_selection_prev();
        }
        return;
    }

    /* Rotary encoder */
    if (evt->type == INPUT_EV_REL && evt->code == INPUT_REL_X) {
        int delta = (evt->value > 0) ? 1 : -1;
        const char *dir_str = (delta > 0) ? "CW" : "CCW";

        if (anim_fixed_bot != NULL) {
            /* Bend angle screen — encoder controls angle */
            sel_bend_angle += delta;
            if (sel_bend_angle < 0)  sel_bend_angle = 0;
            if (sel_bend_angle > 90) sel_bend_angle = 90;
            bend_anim_set_angle(sel_bend_angle);
            printk("Encoder: %s — bend angle %d deg\n", dir_str, sel_bend_angle);
            return;
        }

        if (thick_wood_bot != NULL) {
            /* Thickness screen — encoder toggles between 1/16 and 1/8 mm */
            sel_thickness ^= 1;
            thick_anim_set(sel_thickness);
            printk("Encoder: %s — thickness %s\n", dir_str,
                   sel_thickness == 1 ? "1/8 mm" : "1/16 mm");
            return;
        }

        k_mutex_lock(&encoder_mutex, K_FOREVER);
        position += delta;
        direction = delta;
        k_mutex_unlock(&encoder_mutex);
        printk("Encoder: %s — position %d\n", dir_str, position);
    }
}
INPUT_CALLBACK_DEFINE(NULL, encoder_cb, NULL);

/* encoder API */

int encoder_get_position(void) { return position; }

int encoder_get_direction(void)
{
    k_mutex_lock(&encoder_mutex, K_FOREVER);
    int dir = direction;
    direction = 0;
    k_mutex_unlock(&encoder_mutex);
    return dir;
}

/* bend angle animation */

static void bend_anim_set_angle(int angle_deg)
{
    if (angle_deg < 0)  angle_deg = 0;
    if (angle_deg > 90) angle_deg = 90;

    int dx = (ANIM_BAR_LEN * COS_LUT(angle_deg)) / 1000;
    int dy = (ANIM_BAR_LEN * sin_lut[angle_deg]) / 1000;

    anim_fixed_bot_pts[0].x = ANIM_PIVOT_X;
    anim_fixed_bot_pts[0].y = ANIM_PIVOT_Y + ANIM_LAYER_GAP;
    anim_fixed_bot_pts[1].x = ANIM_PIVOT_X + ANIM_BAR_LEN;
    anim_fixed_bot_pts[1].y = ANIM_PIVOT_Y + ANIM_LAYER_GAP;

    anim_fixed_top_pts[0].x = ANIM_PIVOT_X;
    anim_fixed_top_pts[0].y = ANIM_PIVOT_Y;
    anim_fixed_top_pts[1].x = ANIM_PIVOT_X + ANIM_BAR_LEN;
    anim_fixed_top_pts[1].y = ANIM_PIVOT_Y;

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

    if (anim_value_label != NULL) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d deg", angle_deg);
        lv_label_set_text(anim_value_label, buf);
    }
}

static void bend_anim_create(lv_obj_t *parent)
{
    init_line_styles();

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

    anim_value_label = lv_label_create(parent);
    lv_label_set_text(anim_value_label, "0 deg");
    lv_obj_add_style(anim_value_label, &style_title, 0);
    lv_obj_align(anim_value_label, LV_ALIGN_CENTER, 60, 20);
}

/* thickness animation */
static void thick_anim_set(int idx)
{
    int gap = (idx == 1) ? THICK_GAP_1_8 : THICK_GAP_1_16;
    int acrylic_y = THICK_BOT_Y - gap - THICK_LAYER_GAP;

    thick_wood_bot_pts[0].x = THICK_X;
    thick_wood_bot_pts[0].y = THICK_BOT_Y + THICK_LAYER_GAP;
    thick_wood_bot_pts[1].x = THICK_X + THICK_BAR_LEN;
    thick_wood_bot_pts[1].y = THICK_BOT_Y + THICK_LAYER_GAP;

    thick_wood_top_pts[0].x = THICK_X;
    thick_wood_top_pts[0].y = THICK_BOT_Y;
    thick_wood_top_pts[1].x = THICK_X + THICK_BAR_LEN;
    thick_wood_top_pts[1].y = THICK_BOT_Y;

    thick_acrylic_bot_pts[0].x = THICK_X;
    thick_acrylic_bot_pts[0].y = acrylic_y + THICK_LAYER_GAP;
    thick_acrylic_bot_pts[1].x = THICK_X + THICK_BAR_LEN;
    thick_acrylic_bot_pts[1].y = acrylic_y + THICK_LAYER_GAP;

    thick_acrylic_top_pts[0].x = THICK_X;
    thick_acrylic_top_pts[0].y = acrylic_y;
    thick_acrylic_top_pts[1].x = THICK_X + THICK_BAR_LEN;
    thick_acrylic_top_pts[1].y = acrylic_y;

    if (thick_wood_bot    != NULL) lv_line_set_points(thick_wood_bot,    thick_wood_bot_pts,    2);
    if (thick_wood_top    != NULL) lv_line_set_points(thick_wood_top,    thick_wood_top_pts,    2);
    if (thick_acrylic_bot != NULL) lv_line_set_points(thick_acrylic_bot, thick_acrylic_bot_pts, 2);
    if (thick_acrylic_top != NULL) lv_line_set_points(thick_acrylic_top, thick_acrylic_top_pts, 2);

    if (thick_value_label != NULL) {
        lv_label_set_text(thick_value_label, (idx == 1) ? "1/8 mm" : "1/16 mm");
    }
}

static void thick_anim_create(lv_obj_t *parent)
{
    init_line_styles();
    sel_thickness = 0; /* always start at 1/16 mm */

    int acrylic_y = THICK_BOT_Y - THICK_GAP_1_16 - THICK_LAYER_GAP;

    thick_wood_bot_pts[0].x = THICK_X;
    thick_wood_bot_pts[0].y = THICK_BOT_Y + THICK_LAYER_GAP;
    thick_wood_bot_pts[1].x = THICK_X + THICK_BAR_LEN;
    thick_wood_bot_pts[1].y = THICK_BOT_Y + THICK_LAYER_GAP;
    thick_wood_bot = lv_line_create(parent);
    lv_line_set_points(thick_wood_bot, thick_wood_bot_pts, 2);
    lv_obj_add_style(thick_wood_bot, &style_line_wood, 0);

    thick_wood_top_pts[0].x = THICK_X;
    thick_wood_top_pts[0].y = THICK_BOT_Y;
    thick_wood_top_pts[1].x = THICK_X + THICK_BAR_LEN;
    thick_wood_top_pts[1].y = THICK_BOT_Y;
    thick_wood_top = lv_line_create(parent);
    lv_line_set_points(thick_wood_top, thick_wood_top_pts, 2);
    lv_obj_add_style(thick_wood_top, &style_line_acrylic, 0);

    thick_acrylic_bot_pts[0].x = THICK_X;
    thick_acrylic_bot_pts[0].y = acrylic_y + THICK_LAYER_GAP;
    thick_acrylic_bot_pts[1].x = THICK_X + THICK_BAR_LEN;
    thick_acrylic_bot_pts[1].y = acrylic_y + THICK_LAYER_GAP;
    thick_acrylic_bot = lv_line_create(parent);
    lv_line_set_points(thick_acrylic_bot, thick_acrylic_bot_pts, 2);
    lv_obj_add_style(thick_acrylic_bot, &style_line_wood, 0);

    thick_acrylic_top_pts[0].x = THICK_X;
    thick_acrylic_top_pts[0].y = acrylic_y;
    thick_acrylic_top_pts[1].x = THICK_X + THICK_BAR_LEN;
    thick_acrylic_top_pts[1].y = acrylic_y;
    thick_acrylic_top = lv_line_create(parent);
    lv_line_set_points(thick_acrylic_top, thick_acrylic_top_pts, 2);
    lv_obj_add_style(thick_acrylic_top, &style_line_acrylic, 0);

    thick_value_label = lv_label_create(parent);
    lv_label_set_text(thick_value_label, "1/16 mm");
    lv_obj_add_style(thick_value_label, &style_title, 0);
    lv_obj_align(thick_value_label, LV_ALIGN_RIGHT_MID, -10, 0);
}

/* intialize display screens */

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

    /* encoder */
    const struct device *enc_dev = DEVICE_DT_GET(DT_NODELABEL(encoder));
    if (device_is_ready(enc_dev)) {
        printk("Encoder: ready (PA0=A, PA1=B)\n");
    } else {
        printk("Encoder: NOT ready — check overlay gpio-qdec node\n");
    }

    /* buttons */
    const struct device *btn_dev = DEVICE_DT_GET(DT_NODELABEL(buttons));
    if (device_is_ready(btn_dev)) {
        printk("Buttons: ready (PA8=forward, PA9=backward)\n");
    } else {
        printk("Buttons: NOT ready — check overlay gpio-keys node\n");
    }

    init_styles();
    return 0;
}

/* SCREEN 1 — direction */
void direction_screen(void)
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
    lv_obj_align(steps, LV_ALIGN_TOP_LEFT, 30, 35);

    lv_obj_t *estop = lv_label_create(lv_scr_act());
    lv_label_set_long_mode(estop, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(estop, lv_pct(100));
    lv_label_set_text(estop,
        "* E-stop on right side of machine for emergency");
    lv_obj_add_style(estop, &style_note, 0);
    lv_obj_align(estop, LV_ALIGN_BOTTOM_LEFT, 13, -18);
}

/* SCREEN 2 — BEND RADIUS REMINDER */
void bend_radius_screen(void)
{
    printk("display: bend radius screen\n");
    clear_screen();
    on_radius_screen = true;

    make_label(lv_scr_act(), &style_subtitle, "Set Bend Radius",
               LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *msg = lv_label_create(lv_scr_act());
    lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msg, lv_pct(85));
    lv_obj_set_height(msg, LV_SIZE_CONTENT);
    lv_label_set_text(msg,
        "Before proceeding, manually adjust the bend radius knob (2) "
        "to your desired radius and tighten the locking nuts to secure it.");
    lv_obj_add_style(msg, &style_body, 0);
    lv_obj_align(msg, LV_ALIGN_CENTER, 0, -10);

    make_label(lv_scr_act(), &style_note,
               "Hit " LV_SYMBOL_PLAY " when ready\n"
               "Hit " LV_SYMBOL_PREV " to return",
               LV_ALIGN_BOTTOM_MID, 0, -15);
}

/* SCREEN 3 — bend angle input */
void bend_angle_input_screen(void)
{
    printk("display: bend angle input screen\n");
    clear_screen();

    make_label(lv_scr_act(), &style_note, "1 / 3",
               LV_ALIGN_TOP_RIGHT, -10, 10);
    make_label(lv_scr_act(), &style_title, "Bend Angle",
               LV_ALIGN_CENTER, 60, -25);

    bend_anim_create(lv_scr_act());

    make_label(lv_scr_act(), &style_body,
               "Hit " LV_SYMBOL_PLAY " to proceed\n"
               "Hit " LV_SYMBOL_PREV " to return",
               LV_ALIGN_BOTTOM_MID, 0, -15);

    sel_bend_angle = 0;
    bend_anim_set_angle(0);
}

/* SCREEN 4 — acrylic thickness selection */
void thickness_screen(void)
{
    printk("display: thickness screen\n");
    clear_screen();

    make_label(lv_scr_act(), &style_note, "3 / 3",
               LV_ALIGN_TOP_RIGHT, -10, 10);
    make_label(lv_scr_act(), &style_title, "Thickness",
               LV_ALIGN_CENTER, 60, -25);

    thick_anim_create(lv_scr_act());

    make_label(lv_scr_act(), &style_body,
               "Turn to toggle thickness\n"
               "Hit " LV_SYMBOL_PLAY " to confirm\n"
               "Hit " LV_SYMBOL_PREV " to return",
               LV_ALIGN_BOTTOM_MID, 0, -15);
}

typedef enum {
    INPUT_STEP_BEND_RADIUS = 0,
    INPUT_STEP_BEND_ANGLE,
    INPUT_STEP_THICKNESS,
    INPUT_STEP_COUNT
} input_step_t;

static input_step_t input_step = INPUT_STEP_BEND_RADIUS;

static void input_show_step(input_step_t step)
{
    switch (step) {
    case INPUT_STEP_BEND_RADIUS:
        bend_radius_screen();
        break;
    case INPUT_STEP_BEND_ANGLE:
        bend_angle_input_screen();
        break;
    case INPUT_STEP_THICKNESS:
        thickness_screen();
        break;
    default:
        break;
    }
}

void input_selection_enter(void)
{
    input_step = INPUT_STEP_BEND_RADIUS;
    input_show_step(input_step);
}

int input_selection_next(void)
{
    if (input_step < INPUT_STEP_THICKNESS) {
        input_step++;
        input_show_step(input_step);
        return 0;
    }
    /* Last step confirmed */
    printk("Input confirmed — bend angle: %d deg, thickness: %s\n",
           sel_bend_angle, sel_thickness == 1 ? "1/8 mm" : "1/16 mm");
    return 1;
}

int input_selection_prev(void)
{
    if (input_step > INPUT_STEP_BEND_RADIUS) {
        input_step--;
        input_show_step(input_step);
        return 0;
    }
    /* First step — signal exit to state machine */
    printk("Input cancelled\n");
    return -1;
}

void display_create_home_screen(void) { direction_screen(); }

void display_update(void)
{
    lv_task_handler();
}
