#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "stepper/drv8452_spi.h"
#include "stepper/stepper.h"
#include "temp/temp_control.h"
#include "stepper/limit_sw.h"

int main(void)
{
    printk("=== AcrylicBender boot ===\n");

    if (drv8452_spi_init() != 0) {
        printk("drv8452_spi_init failed\n");
        return -1;
    }
    printk("=== Stepper test sequence ===\n");
    printk("Move: +90 degrees at 20 RPM\n");
    stepper_move_degrees(-900.0f, 20.0f);
    drv8452_disable();

    k_msleep(1000);
    drv8452_enable();
    printk("Move: -90 degrees at 20 RPM\n");
    stepper_move_degrees(900.0f, 20.0f);
    k_msleep(1000);




    // temp_init();
    // heater_start();
    // limit_sw_init();

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
