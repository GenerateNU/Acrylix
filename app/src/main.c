#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/* Smoke test: toggle PB5 at 0.5Hz to confirm MCU is executing.
   Probe PB5 with a multimeter — expect 1s HIGH / 1s LOW. */

#define SMOKE_PIN_NODE DT_NODELABEL(gpiob)
#define SMOKE_PIN      5

int main(void)
{
    const struct device *gpiob = DEVICE_DT_GET(DT_NODELABEL(gpiob));

    if (!device_is_ready(gpiob)) {
        return 0;
    }

    gpio_pin_configure(gpiob, SMOKE_PIN, GPIO_OUTPUT_INACTIVE);

    while (1) {
        gpio_pin_set(gpiob, SMOKE_PIN, 1);
        k_msleep(1000);
        gpio_pin_set(gpiob, SMOKE_PIN, 0);
        k_msleep(1000);
    }

    return 0;
}
