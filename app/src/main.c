#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#define SMOKE_PIN_NODE DT_NODELABEL(gpiob)
#define SMOKE_PIN      5

int main(void)
{
    printk("project starting\n");

    const struct device *gpiob = DEVICE_DT_GET(DT_NODELABEL(gpiob));

    if (!device_is_ready(gpiob)) {
        printk("gpiob not ready\n");
        return 0;
    }

    gpio_pin_configure(gpiob, SMOKE_PIN, GPIO_OUTPUT_INACTIVE);

    while (1) {
        printk("pin toggling\n");
        gpio_pin_set(gpiob, SMOKE_PIN, 1);
        k_msleep(1000);
        gpio_pin_set(gpiob, SMOKE_PIN, 0);
        k_msleep(1000);
    }

    return 0;
}
