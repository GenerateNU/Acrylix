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
    printk("creating bend angle input screen\n ");

    /* title */
    lv_obj_t *angle_title = lv_label_create(lv_scr_act());
    lv_label_set_text(angle_title, "Heating... ");
    lv_obj_align(angle_title, LV_ALIGN_TOP_MID, 0, 10);

    /*angle value*/
    lv_obj_t *temp_value = lv_label_create(lv_scr_act());
    lv_label_set_text(temp_value, "90°C");
    lv_obj_align(temp_value, LV_ALIGN_TOP_MID, 10, 10);

    /* time remaining label */
    lv_obj_t *time_label = lv_label_create(lv_scr_act());
    lv_label_set_text(time_label,
        "time remaing min:sec");
    lv_obj_align(time_label, LV_ALIGN_BOTTOM_MID, 0, 20);

    /* Progress bar*/
    bar = lv_bar_create(lv_scr_act());
    lv_obj_set_size(bar, 200, 20);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 30);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 100, LV_ANIM_OFF);

    /* percentage label */
    lv_obj_t *percentage_label = lv_label_create(lv_scr_act());
    lv_label_set_text(percentage_label, "0%");
    lv_obj_align(percentage_label, LV_ALIGN_BOTTOM_MID, 0, 50);
}

void test_display(void)
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
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

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

/* create direction screen */
static void direction_screen(void)
{
    printk("creating directions screen\n ");

    /* directions title */
    lv_obj_t *direct_title = lv_label_create(lv_scr_act());
    lv_label_set_text(direct_title, "Directions: ");
    lv_obj_align(direct_title, LV_ALIGN_TOP_MID, 0, 20);

    /* directions paragraph */
    lv_obj_t *directions = lv_label_create(lv_scr_act());
    lv_label_set_text(directions, 
        "1. Insert acrylic on left hand side and align to desired position. "
        "2. Clamp by turning upper clamp knob (1). "
        "3. Manually adjust knob (2) to desired bend radius and tighten nuts to lock. "
        "4. Hit ▶ to proceed and select inputs. "
        "* E-stop on right side of machine for emergency");
    lv_obj_align(directions, LV_ALIGN_TOP_MID, 0, 40);
}

/* create bend angle input screen */
static void bend_angle_screen(void)
{
    /* title */
    lv_obj_t *angle_title = lv_label_create(lv_scr_act());
    lv_label_set_text(angle_title, "Bend Angle: ");
    lv_obj_align(angle_title, LV_ALIGN_TOP_MID, 0, 0);

    /*angle value*/
    lv_obj_t *angle_value = lv_label_create(lv_scr_act());
    lv_label_set_text(angle_value, "0°");
    lv_obj_align(angle_value, LV_ALIGN_TOP_MID, 10, 10);

    lv_obj_t *procced_label = lv_label_create(lv_scr_act());
    lv_label_set_text(procced_label,
        "Hit ▶ to proceed"
        "Hit ◀ to return");
    lv_obj_align(procced_label, LV_ALIGN_CENTER, 0, 0);
}

/* create idle screen */
static void idle_screen(void)
{
    printk("creating initialization screen\n ");
}

/*  create bend screen*/
static void bend_screen(void)
{
    printk("creating bend screen\n ");

    /* bend title */
    lv_obj_t *bend_title = lv_label_create(lv_scr_act());
    lv_label_set_text(bend_title, "Bending...");
    lv_obj_align(bend_title, LV_ALIGN_TOP_MID, 0, 0);
}

/* create cool screen */
static void cool_screen(void)
{
    printk("creating cool screen\n ");

    /* cool title */
    lv_obj_t *cool_title = lv_label_create(lv_scr_act());
    lv_label_set_text(cool_title, "Cooling...");
    lv_obj_align(cool_title, LV_ALIGN_TOP_MID, 0, 0);
}

/* create complete screen */
static void complete_screen(void)
{
    printk("creating complete screen\n ");
}

/* ── Update state label (called from display thread via message queue) ── */
void display_set_state(int state)
{
    /* Cast int back to state type — safe because we control what gets sent */
    system_state_t s = (system_state_t)state;

    lv_label_set_text(state_label, get_state_name(s));

    switch (s) {
        case STATE_IDLE:
            printk("idle \n");
            //idle_screen();
            break;
        case STATE_INITIALIZATION:
            printk("initializing \n");
            break;
        case STATE_BEND:
            printk("bending \n");
            //bend_screen();
            break;
        case STATE_COOL:
            printk("cooling \n");
            //cool_screen();
            break;
        case STATE_COMPLETE:
            printk("complete \n");
            //complete_screen();
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