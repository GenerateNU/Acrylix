#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include "temp/temp_control.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    LOG_INF("=== AcrylicBender boot ===");

    temp_init();
    heater_start();

    // /* SSR toggle test — PC9 high/low every 2 seconds */
    // const struct device *gpioc = DEVICE_DT_GET(DT_NODELABEL(gpioc));
    // gpio_pin_configure(gpioc, 9, GPIO_OUTPUT_INACTIVE);

    // while (1) {
    //     gpio_pin_set(gpioc, 9, 1);
    //     LOG_INF("SSR ON");
    //     k_sleep(K_SECONDS(2));

    //     gpio_pin_set(gpioc, 9, 0);
    //     LOG_INF("SSR OFF");
    //     k_sleep(K_SECONDS(2));
    // }

    return 0;
}
