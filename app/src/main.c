#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "temp/temp_sensor.h"
// #include "stepper/drv8452_spi.h"
// #include "stepper/stepper.h"

int main(void)
{
    printk("=== AcrylicBender boot ===\n");

    temp_thread_init();

    // if (drv8452_spi_init() != 0) {
    //     printk("drv8452_spi_init failed\n");
    //     return -1;
    // }

    // if (stepper_init() != 0) {
    //     printk("stepper_init failed\n");
    //     return -1;
    // }

    return 0;
}
