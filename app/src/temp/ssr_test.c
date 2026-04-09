#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ssr_test, LOG_LEVEL_INF);

void ssr_test_run(void)
{
    const struct device *gpioc = DEVICE_DT_GET(DT_NODELABEL(gpioc));

    if (!device_is_ready(gpioc)) {
        LOG_ERR("GPIOC not ready");
        return;
    }

    gpio_pin_configure(gpioc, 9, GPIO_OUTPUT_INACTIVE);

    while (1) {
        gpio_pin_set(gpioc, 9, 1);
        LOG_INF("SSR ON");
        k_sleep(K_SECONDS(2));

        gpio_pin_set(gpioc, 9, 0);
        LOG_INF("SSR OFF");
        k_sleep(K_SECONDS(2));
    }
}
