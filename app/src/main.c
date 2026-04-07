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

    /* Intialize display and UI */
    printk("initializing display\n");
    int ret = display_init();
    if (ret != 0) {
        printk("display_init failed: %d\n", ret);
    } else {
        printk("display initialized, creating home screen\n");
        bend_angle_input_screen();
        printk("home screen created\n");
    }

    /* Raw GPIO read on PA0/PA1 to confirm encoder signals are reaching the MCU */
    const struct device *gpioa = DEVICE_DT_GET(DT_NODELABEL(gpioa));
    gpio_pin_configure(gpioa, 0, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_configure(gpioa, 1, GPIO_INPUT | GPIO_PULL_UP);

    printk("entering main loop\n");
    int last_a = -1, last_b = -1;
    while (1) {
        display_update();
        gpio_pin_toggle(gpiob, SMOKE_PIN);

        int a = gpio_pin_get(gpioa, 0);
        int b = gpio_pin_get(gpioa, 1);
        if (a != last_a || b != last_b) {
            printk("GPIO raw: PA0=%d PA1=%d\n", a, b);
            last_a = a;
            last_b = b;
        }

        k_msleep(10);
    }

    return 0;
}
