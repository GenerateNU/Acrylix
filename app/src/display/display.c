/* display.c */
#include "display.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <zephyr/input/input.h>
#include <lvgl.h>
#include <stdio.h>
#include "../states/states.h"

/* ── Display device ── */
static const struct device *display_dev;

/* ── UI elements ── */
static lv_obj_t *position_label;
static lv_obj_t *direction_label;
static lv_obj_t *state_label;
static lv_obj_t *bar;

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
static bool styles_initialized = false;

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

static void clear_screen(void)
{
    bar = NULL;
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

int encoder_get_position(void)
{
    return position;
}

int encoder_get_direction(void)
{
    k_mutex_lock(&encoder_mutex, K_FOREVER);
    int dir = direction;
    direction = 0;
    k_mutex_unlock(&encoder_mutex);
    return dir;
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
 *  Screens — all static (internal only)
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
        "4. Hit  " LV_SYMBOL_PLAY " to proceed and select inputs.");
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
static void input_selection_screen(void)
{
    printk("display: input selection screen\n");
    clear_screen();
    make_label(lv_scr_act(), &style_title, "Input Selection",
               LV_ALIGN_TOP_MID, 0, 10);
    /* TODO: bend angle, thickness, bend radii inputs with encoder */
}

/* SCREEN 3 — PROCESS (STATE_BEND / STATE_COOL) */
static void process_screen(const char *header, const char *value)
{
    printk("display: process screen (%s)\n", header);
    clear_screen();

    make_label(lv_scr_act(), &style_title, header, LV_ALIGN_TOP_MID,  0, 10);
    make_label(lv_scr_act(), &style_title, value,  LV_ALIGN_TOP_MID,  0, 35);
    make_label(lv_scr_act(), &style_body,  "time remaining --:--",
               LV_ALIGN_BOTTOM_MID, 0, -60);

    bar = lv_bar_create(lv_scr_act());
    lv_obj_set_size(bar, 200, 20);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -35);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);

    make_label(lv_scr_act(), &style_body, "0%", LV_ALIGN_BOTTOM_MID, 0, -10);
}

/* SCREEN 4 — COMPLETE (STATE_COMPLETE) */
static void complete_screen(void)
{
    printk("display: complete screen\n");
    clear_screen();
    make_label(lv_scr_act(), &style_title, "Done!",
               LV_ALIGN_CENTER, 0, -10);
    make_label(lv_scr_act(), &style_body,  "Safe to remove.",
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

void display_create_home_screen(void)
{
    direction_screen();
}

void display_set_state(int state)
{
    switch ((system_state_t)state) {
        case STATE_IDLE:           direction_screen();                     break;
        case STATE_INITIALIZATION: input_selection_screen();               break;
        case STATE_BEND:           process_screen("Bending...", "15\xC2\xB0"); break;
        case STATE_COOL:           process_screen("Cooling...", "");       break;
        case STATE_COMPLETE:       complete_screen();                      break;
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