#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "stepper/drv8452_spi.h"
#include "stepper/stepper.h"

/* Smoke indicator: PB5 blinks while the board is alive.
   Probe with multimeter — 1 s HIGH / 1 s LOW during each inter-move pause. */
#define SMOKE_PIN 5

int main(void)
{
    const struct device *gpiob = DEVICE_DT_GET(DT_NODELABEL(gpiob));

    if (!device_is_ready(gpiob)) {
        return -1;
    }

    gpio_pin_configure(gpiob, SMOKE_PIN, GPIO_OUTPUT_INACTIVE);

    if (drv8452_spi_init() != 0) {
        printk("drv8452_spi_init failed\n");
        return -1;
    }

    if (stepper_init() != 0) {
        printk("stepper_init failed\n");
        return -1;
    }

    while (1) {
        /* 1 s pause with blink before forward move */
        gpio_pin_set(gpiob, SMOKE_PIN, 1);
        k_msleep(1000);
        gpio_pin_set(gpiob, SMOKE_PIN, 0);

        printk("Moving to 360 degrees\n");
        stepper_move_to_degrees(360.0f);

        /* 1 s pause with blink before return move */
        k_msleep(500);
        gpio_pin_set(gpiob, SMOKE_PIN, 1);
        k_msleep(1000);
        gpio_pin_set(gpiob, SMOKE_PIN, 0);

        printk("Moving to 0 degrees\n");
        stepper_move_to_degrees(0.0f);

        k_msleep(500);
    }

    return 0;
}
