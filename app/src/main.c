#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
// #include "display/display.h"

// #define BTN_FWD_PIN 8
// #define BTN_BCK_PIN 9

static const struct device *gpioc;
static volatile int32_t enc_count = 0;
static struct gpio_callback enc_cb_data;

static void enc_isr(const struct device *dev, struct gpio_callback *cb,
                    uint32_t pins)
{
    int a = gpio_pin_get(gpioc, 10);
    int b = gpio_pin_get(gpioc, 11);
    /* CW: A leads B — on A rising edge, B is low */
    /* CCW: B leads A — on A rising edge, B is high */
    if (a == b) {
        enc_count--;
    } else {
        enc_count++;
    }
}

static void encoder_init(void)
{
    gpioc = DEVICE_DT_GET(DT_NODELABEL(gpioc));
    if (!device_is_ready(gpioc)) {
        printk("GPIOC not ready\n");
        return;
    }
    gpio_pin_configure(gpioc, 10, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_configure(gpioc, 11, GPIO_INPUT | GPIO_PULL_UP);

    gpio_pin_interrupt_configure(gpioc, 10, GPIO_INT_EDGE_BOTH);

    gpio_init_callback(&enc_cb_data, enc_isr, BIT(10));
    gpio_add_callback(gpioc, &enc_cb_data);
}

int main(void)
{
    printk("encoder test starting\n");

    // /* Button test */
    // const struct device *gpioa = DEVICE_DT_GET(DT_NODELABEL(gpioa));
    // gpio_pin_configure(gpioa, BTN_BCK_PIN, GPIO_INPUT | GPIO_PULL_DOWN);
    // printk("reading PA8 (forward button)...\n");
    // int last = -1;
    // while (1) {
    //     int val = gpio_pin_get(gpioa, BTN_BCK_PIN);
    //     if (val != last) {
    //         printk("BTN_FWD (PA8) = %d\n", val);
    //         last = val;
    //     }
    //     k_msleep(10);
    // }

    encoder_init();

    int32_t last = 0;
    while (1) {
        if (enc_count != last) {
            printk("ENC count: %d\n", enc_count);
            last = enc_count;
        }
        k_msleep(200);
    }

    // return 0;
}
