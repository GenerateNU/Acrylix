#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

/*Pin definitions -- pin config in .overlay*/
#define STEP_NODE DT_ALIAS(stepper_step)
#define DIR_NODE DT_ALIAS(stepper_dir)

static const struct gpio_dt_spec step_pin = GPIO_DT_SPEC_GET(STEP_NODE, gpios);
static const struct gpio_dt_spec dir_pin = GPIO_DT_SPEC_GET(DIR_NODE, gpios);

/* Motor config*/
#define MICROSTEPS 100
#define STEPS_PER_REV 20000
#define TARGET_RPM 100.0f

#define STEP_DELAY_US   ((uint32_t)((60.0f / TARGET_RPM / STEPS_PER_REV) * 1000000.0f / 2.0f))

#define DIR_FORWARD 1
#define DIR_BACKWARD 0

static void rotate_steps(long steps, int direction)
{
    gpio_pin_set(&dir_pin, direction);
    k_msleep(5); // DIR must settle ≥5 µs before first pulse

    for (long i = 0; i < steps; i++) {
        gpio_pin_set_dt(&step_pin, 1);
        k_busy_wait(STEP_DELAY_US);
        gpio_pin_set_dt(&step_pin, 0);
        k_busy_wait(STEP_DELAY_US);
    }
}

int main(void)
{
    int ret;

     /* Check if GPIO devices are ready */
    if (!gpio_is_ready_dt(&step_pin)) {
        printk("STEP GPIO not ready\n");
        return -1;
    }
    if (!gpio_is_ready_dt(&dir_pin)) {
        printk("DIR GPIO not ready\n");
        return -1;
    }

   /* Configure pins as outputs */
    ret = gpio_pin_configure_dt(&step_pin, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) { printk("Failed to configure STEP pin\n"); return ret; }

    ret = gpio_pin_configure_dt(&dir_pin, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) { printk("Failed to configure DIR pin\n");  return ret; }

    printk("Stepper init OK\n");
    printk("RPM:           %.1f\n", (double)TARGET_RPM);
    printk("Steps/rev:     %d\n",   STEPS_PER_REV);
    printk("Step delay us: %u\n",   STEP_DELAY_US);

    while (1) {
        /* 1 full revolution forward */
        printk("Forward...\n");
        rotate_steps(STEPS_PER_REV, DIR_FORWARD);
        k_sleep(K_MSEC(1000));

        /* 1 full revolution backward */
        printk("Backward...\n");
        rotate_steps(STEPS_PER_REV, DIR_BACKWARD);
        k_sleep(K_MSEC(1000));
    }

    return 0;
}