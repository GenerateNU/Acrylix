#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>

extern void lv_demo_widgets(void);

/* Get LED from device tree */
const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

int main(void)
{
    printk("project starting...\n");

    const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));


    if (!device_is_ready(display)) {
        gpio_pin_toggle_dt(&led);
        k_msleep(200);
        return -1;
    }

    //lv_demo_widgets();

    k_msleep(500);
    
    display_blanking_off(display);

    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);
    
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "display working");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    while (1) {
        lv_task_handler();
        k_msleep(10);
    }

    
    /* testing led flash*/
    
    gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);

    /*while (1) {
        gpio_pin_toggle_dt(&led);
        k_msleep(500);
    } */

    return 0;
}
