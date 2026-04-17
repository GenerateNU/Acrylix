#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "ssr.h"
#include "../states/states.h"

static const struct device *gpioc;

void ssr_init(void)
{
    gpioc = DEVICE_DT_GET(DT_NODELABEL(gpioc));

    if (!device_is_ready(gpioc)) {
        printk("ERR: GPIOC not ready\n");
        return;
    }

    gpio_pin_configure(gpioc, 9, GPIO_OUTPUT_HIGH);   /* HIGH = SSR off (active-low) */
    printk("SSR init OK (PC9)\n");
}

void ssr_set(bool on)
{
    if (on && g_sm.current != STATE_HEAT) {
        gpio_pin_set(gpioc, 9, 1);  /* force off */
        printk("SSR blocked — not in HEAT state\n");
        return;
    }
    gpio_pin_set(gpioc, 9, on ? 0 : 1);
    printk("SSR %s\n", on ? "ON" : "OFF");
}
