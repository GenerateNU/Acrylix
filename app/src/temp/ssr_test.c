#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

void ssr_test_run(void)
{
    const struct device *gpioc = DEVICE_DT_GET(DT_NODELABEL(gpioc));

    if (!device_is_ready(gpioc)) {
        printk("ERR: GPIOC not ready\n");
        return;
    }

    gpio_pin_configure(gpioc, 9, GPIO_OUTPUT_INACTIVE);

    while (1) {
        gpio_pin_set(gpioc, 9, 1);
        printk("SSR ON\n");
        k_sleep(K_SECONDS(2));

        gpio_pin_set(gpioc, 9, 0);
        printk("SSR OFF\n");
        k_sleep(K_SECONDS(2));
    }
}
