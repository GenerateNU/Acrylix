#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "ssr.h"

LOG_MODULE_REGISTER(ssr, LOG_LEVEL_INF);

static const struct device *gpioc;

void ssr_init(void)
{
    gpioc = DEVICE_DT_GET(DT_NODELABEL(gpioc));

    if (!device_is_ready(gpioc)) {
        LOG_ERR("GPIOC not ready");
        return;
    }

    gpio_pin_configure(gpioc, 9, GPIO_OUTPUT_INACTIVE);
    LOG_INF("SSR init OK (PC9)");
}

void ssr_set(bool on)
{
    gpio_pin_set(gpioc, 9, on ? 0 : 1);
    LOG_INF("SSR %s", on ? "ON" : "OFF");
}
