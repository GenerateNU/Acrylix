#include "display.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <zephyr/input/input.h> 
#include <lvgl.h>
#include <stdio.h>
#include "../states/states.h"

/* ── Display ── */
static const struct device *display_dev;

/* UI Elements*/
static lv_obj_t *position_label;
static lv_obj_t *direction_label;
static lv_obj_t *state_label;
static lv_obj_t *bar;

/* ── Encoder state ── */
static int position = 0;
static int direction = 0;

static K_MUTEX_DEFINE(encoder_mutex);

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

/* Get encoder position */
int encoder_get_position(void)
{
    return position;
}

/* Get encoder direction */
int encoder_get_direction(void)
{
    k_mutex_lock(&encoder_mutex, K_FOREVER);
    int dir = direction;
    direction = 0;
    k_mutex_unlock(&encoder_mutex);
    return dir;
}

/* Initialize display */
int display_init(void)
{
    lv_init();
    display_dev = DEVICE_DT_GET(DT_NODELABEL(ili9341));

    if (!device_is_ready(display_dev)) {
        printk("Display not ready\n");
        return -1;
    }

    display_blanking_off(display_dev);
    printk("Display initialized\n");

    struct display_capabilities caps;
    display_get_capabilities(display_dev, &caps);
    printk("Display width: %d height: %d\n", caps.x_resolution, caps.y_resolution);

    const struct device *enc_dev = DEVICE_DT_GET(DT_NODELABEL(encoder));
    if (!device_is_ready(enc_dev)) {
        printk("Encoder device NOT ready!\n");
    } else {
        printk("Encoder device ready\n");
    }
    return 0;
}

/* Creates UI screen */
void display_create_home_screen(void)
{
    printk("create home screen\n");
    lv_obj_clean(lv_scr_act());

    state_label = lv_label_create(lv_scr_act());
    lv_label_set_text(state_label, "IDLE");
    lv_obj_align(state_label, LV_ALIGN_TOP_MID, 10, 10);
    lv_obj_set_style_text_font(state_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(state_label, lv_color_hex(0x00FF00), LV_PART_MAIN);

    /* Title */
    lv_obj_t *title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, "Encoder Position");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);

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

    /* Progress bar*/
    bar = lv_bar_create(lv_scr_act());
    lv_obj_set_size(bar, 200, 20);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 30);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 100, LV_ANIM_OFF);
}

/* ── Update state label (called from display thread via message queue) ── */
void display_set_state(int state)
{
    /* Cast int back to state type — safe because we control what gets sent */
    system_state_t s = (system_state_t)state;

    lv_label_set_text(state_label, get_state_name(s));

    /* Change color per state */
    switch (s) {
        case STATE_IDLE:
            lv_obj_set_style_text_color(state_label,
                lv_color_hex(0x00FF00), LV_PART_MAIN);  // green
            break;
        case STATE_INITIALIZATION:
        case STATE_HOMING:
            lv_obj_set_style_text_color(state_label,
                lv_color_hex(0xFFFF00), LV_PART_MAIN);  // yellow
            break;
        case STATE_BEND:
        case STATE_COOL:
            lv_obj_set_style_text_color(state_label,
                lv_color_hex(0x00BFFF), LV_PART_MAIN);  // blue
            break;
        case STATE_COMPLETE:
            lv_obj_set_style_text_color(state_label,
                lv_color_hex(0x00FF00), LV_PART_MAIN);  // green
            break;
        case STATE_ERROR:
            lv_obj_set_style_text_color(state_label,
                lv_color_hex(0xFF0000), LV_PART_MAIN);  // red
            break;
        default:
            break;
    }
}

/* Updates the display */
void display_update(void)
{
    /* Safely read encoder state and push to LVGL */
    k_mutex_lock(&encoder_mutex, K_FOREVER);
    int pos = position;
    int dir = direction;
    k_mutex_unlock(&encoder_mutex);

    char pos_str[16];
    snprintf(pos_str, sizeof(pos_str), "%d", pos);
    lv_label_set_text(position_label, pos_str);
    lv_bar_set_value(bar, pos, LV_ANIM_ON);

    if (dir == 1) {
        lv_label_set_text(direction_label, "CW  >>>");
        lv_obj_set_style_text_color(direction_label,
            lv_color_hex(0x00FF00), LV_PART_MAIN);
    } else if (dir == -1) {
        lv_label_set_text(direction_label, "<<< CCW");
        lv_obj_set_style_text_color(direction_label,
            lv_color_hex(0xFF4500), LV_PART_MAIN);
    }

    lv_task_handler();
}