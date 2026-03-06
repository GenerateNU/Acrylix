#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <lvgl.h>
#include <zephyr/input/input.h>
#include "display.h"

extern void lv_demo_widgets(void);

/* Get LED from device tree */
const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
const struct device *enc = DEVICE_DT_GET(DT_NODELABEL(encoder));

static void any_input_cb(struct input_event *evt, void *user_data)
{
    printk("Input event: type=%d code=%d value=%d\n",
           evt->type, evt->code, evt->value);
}
INPUT_CALLBACK_DEFINE(NULL, any_input_cb, NULL);

int main(void)
{
    printk("project starting...\n");

    //const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));


    if (display_init() != 0) {
        printk("could not initalize");
        gpio_pin_toggle_dt(&led);
        k_msleep(200);
        return -1;
    }

    display_create_home_screen();

    while (1) {
        display_update();
        k_msleep(10);
    }

    
    /* testing led flash*/
    
    //gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);

    /*while (1) {
        gpio_pin_toggle_dt(&led);
        k_msleep(500);
    } */

    return 0;
}
