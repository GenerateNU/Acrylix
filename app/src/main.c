#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "display/display.h"

#define SMOKE_PIN_NODE DT_NODELABEL(gpiob)
#define SMOKE_PIN      5

int main(void)
{
    printk("project starting\n");

    /* Smoke test — PB5 toggles every 500ms to confirm MCU is running */
    printk("initializing smoke test pin PB5\n");
    const struct device *gpiob = DEVICE_DT_GET(DT_NODELABEL(gpiob));
    if (device_is_ready(gpiob)) {
        gpio_pin_configure(gpiob, SMOKE_PIN, GPIO_OUTPUT_INACTIVE);
        printk("smoke test pin ready\n");
    } else {
        printk("smoke test pin not ready\n");
    }

    printk("initializing display\n");
    int ret = display_init();
    if (ret != 0) {
        printk("display_init failed: %d\n", ret);
    } else {
        printk("display initialized, creating home screen\n");
        display_create_home_screen();
        printk("home screen created\n");
    }

    printk("entering main loop\n");
    while (1) {
        display_update();
        gpio_pin_toggle(gpiob, SMOKE_PIN);
        k_msleep(500);
    }

    return 0;
}
