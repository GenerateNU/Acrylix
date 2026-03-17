#include "display.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <zephyr/input/input.h> 
#include <lvgl.h>
#include <stdio.h>

/* ── Display ── */
static const struct device *display_dev;

/* UI Elements*/
static lv_obj_t *position_label;
static lv_obj_t *direction_label;
static lv_obj_t *bar;

/* ── Encoder state ── */
static int position = 0;
static int direction = 0;

#if 1
static void encoder_cb(struct input_event *evt, void *user_data)
{
    ARG_UNUSED(user_data);
    
    printk("Event type: %d code: %d value: %d\n", evt->type, evt->code, evt->value);

    /* Reads encoder input and changes position and direction value*/
    if (evt->type == INPUT_EV_REL && evt->code == INPUT_REL_X) {
        if (evt->value > 0) {
            position++;
            if (position > 100){
                position = 100;
            }
            direction = 1;
        }
        else{
            position--;
            if (position < 0) {
                position = 0;
            }
            direction = -1;
        }

        lv_bar_set_value(bar, position, LV_ANIM_ON);    //animates the bar

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
#endif

INPUT_CALLBACK_DEFINE(NULL, encoder_cb, NULL);

/* Get encoder position */
int encoder_get_position(void)
{
    return position;
}

/* Get encoder direction */
int encoder_get_direction(void)
{
    int dir = direction;
    direction = 0;
    return dir;
}

/* Initialize display */
int display_init(void)
{
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
    printk("Pixel format: %d\n", caps.current_pixel_format);

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

    /* Progress bar*/
    bar = lv_bar_create(lv_scr_act());
    lv_obj_set_size(bar, 200, 20);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 30);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 100, LV_ANIM_OFF);
}

/* Updates the display */
void display_update(void)
{
    lv_task_handler();
}