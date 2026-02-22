#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "display.h"

/* Get LED from device tree */
const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

int main(void)
{
    printk("project starting...\n");

    if (display_init() != 0) {
        printk("can't initialize display \n");
        return -1;
    }

    display_create_home_screen();

    while(1){
    	display_update();
    	k_msleep(10);
    }

    /* testing led flash
     *
    gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);

    while (1) {
        gpio_pin_toggle_dt(&led);
        k_msleep(500);
    }*/

    return 0;
}
