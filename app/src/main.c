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
    /*const struct device *gpiob = DEVICE_DT_GET(DT_NODELABEL(gpiob));
    if (device_is_ready(gpiob)) {
        gpio_pin_configure(gpiob, SMOKE_PIN, GPIO_OUTPUT_INACTIVE);
        printk("smoke test pin ready\n");
    } else {
        printk("smoke test pin not ready\n");
    }*/

    /* Intialize display and UI */
    printk("initializing display\n");
    int ret = display_init();
    if (ret != 0) {
        printk("display_init failed: %d\n", ret);
    } else {
        printk("display initialized, creating home screen\n");
        //bend_angle_input_screen();
        input_selection_enter();
        printk("home screen created\n");
    }

    /* Test PB10 backward button */
    const struct device *gpiob = DEVICE_DT_GET(DT_NODELABEL(gpiob));
    gpio_pin_configure(gpiob, 10, GPIO_INPUT);
    gpio_pin_configure(gpiob, 15, GPIO_INPUT);

    int last_fwd = gpio_pin_get_raw(gpiob, 10);
    int last_bwd = gpio_pin_get_raw(gpiob, 15);
    printk("PB10 forward intial: %d\n", last_fwd);
    printk("PB15 backward initial: %d\n", last_bwd);

    printk("entering main loop\n");
    while (1) {
        display_update();

        int fwd = gpio_pin_get_raw(gpiob, 10);
        int bwd = gpio_pin_get_raw(gpiob, 15);

        printk("PB10 (forward): %d  PB15 (backward): %d\n", fwd, bwd);

        k_msleep(200);
    }

    return 0;
}
