#include "limit_sw.h"
#include "stepper.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <stdbool.h>

static const struct device *gpioa;
static struct gpio_callback limit_sw_cb_data;
static volatile bool g_triggered = false;
#define LIMIT_SW_PIN 3
#define DEBOUNCE_MS     50

static void limit_sw_isr(const struct device *dev,
                          struct gpio_callback *cb,
                          uint32_t pins)
{
    static int64_t last_press = 0;
    int64_t now = k_uptime_get();

    if (gpio_pin_get(gpioa, LIMIT_SW_PIN) != 0) {
        return;
    }

    if ((now - last_press) < DEBOUNCE_MS) {
        return;
    }
    last_press = now;

    printk("LIMIT SWITCH: hit — motor stop required\n");
    g_triggered = true;
    stepper_emergency_stop();
}

void limit_sw_init(void)
{
    printk("limit_sw: initializing on PA3\n");

    gpioa = DEVICE_DT_GET(DT_NODELABEL(gpioa));
    if (!device_is_ready(gpioa)) {
        printk("limit_sw: GPIOA not ready\n");
        return;
    }

    gpio_pin_configure(gpioa, LIMIT_SW_PIN, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_interrupt_configure(gpioa, LIMIT_SW_PIN, GPIO_INT_EDGE_FALLING);
    gpio_init_callback(&limit_sw_cb_data, limit_sw_isr, BIT(LIMIT_SW_PIN));
    gpio_add_callback(gpioa, &limit_sw_cb_data);

    printk("limit_sw: ready, waiting for press on PA3\n");
}

bool limit_sw_is_pressed(void)
{
    return gpio_pin_get(gpioa, LIMIT_SW_PIN) == 0;
}

bool limit_sw_triggered(void)
{
    return g_triggered;
}

void limit_sw_clear_trigger(void)
{
    g_triggered = false;
}