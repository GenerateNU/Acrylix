#include "display.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
/* #include <zephyr/input/input.h> */
#include <zephyr/input/input.h> 
#include <lvgl.h>
#include <stdio.h>

/* ── Display ── */
static const struct device *display_dev;
static lv_obj_t *position_label;
static lv_obj_t *direction_label;
static lv_obj_t *state_label;
static lv_obj_t *bar;


/* ── Encoder state ── */
static int position = 0;
static int direction = 0;
static K_MUTEX_DEFINE(encoder_mutex);

/* ── LVGL styles ── */
static lv_style_t style_screen;
static lv_style_t style_title;
static lv_style_t style_subtitle;
static lv_style_t style_body;
static lv_style_t style_note;
static bool styles_initialized = false;


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
    lv_style_set_text_color(&style_note, lv_color_white());
    lv_style_set_text_font(&style_note, &lv_font_montserrat_14);

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

static void encoder_cb(struct input_event *evt)
{
    if (evt->type == INPUT_EV_REL && evt->code == INPUT_REL_X) {
        if (evt->value > 0) {
            position++;
            direction = 1;
        } else {
            position--;
            direction = -1;
        }

        char pos_str[16];
        printk("%d", position);
        printk("%d", direction);
        snprintf(pos_str, sizeof(pos_str), "%d", position);
        lv_label_set_text(position_label, pos_str);

        if (direction == 1) {
            lv_label_set_text(direction_label, "CW  >>>");
        } else {
            lv_label_set_text(direction_label, "<<< CCW");
        }

        printk("Encoder position: %d\n", position);
    }
}

INPUT_CALLBACK_DEFINE(NULL, encoder_cb);

int encoder_get_position(void)
{
    return position;
}

int encoder_get_direction(void)
{
    int dir = direction;
    direction = 0;
    return dir;
}

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

    /* Encoder check omitted — node not yet in overlay */

    init_styles();
    return 0;
}

void display_create_home_screen(void)
{
    printk("create home screen\n");
    lv_obj_clean(lv_scr_act());

    /* Title */
    lv_obj_t *title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, "Encoder Position");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    /* Position number */
    position_label = lv_label_create(lv_scr_act());
    lv_label_set_text(position_label, "0");
    lv_obj_align(position_label, LV_ALIGN_CENTER, 0, -10);

    /* Direction label */
    lv_obj_t *dir_title = lv_label_create(lv_scr_act());
    lv_label_set_text(dir_title, "Direction:");
    lv_obj_align(dir_title, LV_ALIGN_BOTTOM_LEFT, 20, -10);

    direction_label = lv_label_create(lv_scr_act());
    lv_label_set_text(direction_label, "---");
    lv_obj_align(direction_label, LV_ALIGN_BOTTOM_RIGHT, -20, -10);
}

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
    lv_obj_align(steps, LV_ALIGN_TOP_LEFT, 30, 45);

    lv_obj_t *estop = lv_label_create(lv_scr_act());
    lv_label_set_long_mode(estop, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(estop, lv_pct(100));
    lv_label_set_text(estop,
        "* E-stop on right side of machine for emergency");
    lv_obj_add_style(estop, &style_note, 0);
    lv_obj_align(estop, LV_ALIGN_BOTTOM_LEFT, 13, -18);
}

void display_update(void)
{
    lv_task_handler();
}