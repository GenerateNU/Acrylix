#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "stepper/drv8452_spi.h"
#include "stepper/stepper.h"

/* PB5 smoke LED removed — PB5 is now MISO for DRV8452 bit-bang SPI.
 * Restore after DRV8452 debug is complete. */

int main(void)
{
    if (drv8452_spi_init() != 0) {
        printk("drv8452_spi_init failed\n");
        return -1;
    }

    if (stepper_init() != 0) {
        printk("stepper_init failed\n");
        return -1;
    }

    printk("=== Stepper test sequence ===\n");

    printk("Move: +90 degrees at 20 RPM\n");
    stepper_move_degrees(90.0f, 20.0f);
    k_msleep(1000);

    printk("Move: +90 degrees at 20 RPM\n");
    stepper_move_degrees(90.0f, 20.0f);
    k_msleep(1000);

    printk("Move: -180 degrees at 20 RPM\n");
    stepper_move_degrees(-180.0f, 20.0f);
    k_msleep(500);
    drv8452_disable();

    printk("=== Stepper test done ===\n");

    while (1) {
        k_msleep(1000);
    }

    return 0;
}
