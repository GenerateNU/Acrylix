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

    /* Button debug pins — PA8=forward, PA9=backward */
    const struct device *gpioa = DEVICE_DT_GET(DT_NODELABEL(gpioa));
    gpio_pin_configure(gpioa, 8, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_configure(gpioa, 9, GPIO_INPUT | GPIO_PULL_UP);
    printk("Button pins configured: PA8=forward PA9=backward\n");

    printk("entering main loop\n");
    int last_fwd = 1, last_bck = 1; /* active low, idle = 1 */
    while (1) {
        display_update();
        gpio_pin_toggle(gpiob, SMOKE_PIN);

        int fwd = gpio_pin_get(gpioa, 8);
        int bck = gpio_pin_get(gpioa, 9);

        if (fwd != last_fwd) {
            printk("PA8 (forward): %s\n", fwd == 0 ? "PRESSED" : "released");
            last_fwd = fwd;
        }
        if (bck != last_bck) {
            printk("PA9 (backward): %s\n", bck == 0 ? "PRESSED" : "released");
            last_bck = bck;
        }

        k_msleep(10);
    }

    return 0;
}
