#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "temp/temp_control.h"

int main(void)
{
    printk("=== AcrylicBender boot ===\n");

    temp_init();
    heater_start();

    // /* SSR toggle test — PC9 high/low every 2 seconds */
    // const struct device *gpioc = DEVICE_DT_GET(DT_NODELABEL(gpioc));
    // gpio_pin_configure(gpioc, 9, GPIO_OUTPUT_INACTIVE);

    // while (1) {
    //     gpio_pin_set(gpioc, 9, 1);
    //     printk("SSR ON\n");
    //     k_sleep(K_SECONDS(2));

    //     gpio_pin_set(gpioc, 9, 0);
    //     printk("SSR OFF\n");
    //     k_sleep(K_SECONDS(2));
    // }

    return 0;
}
