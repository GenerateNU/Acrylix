/*
 * NEMA 23 stepper via DM556T
 *
 * Motor : 23HS22-4004-ME1K
 * Driver: DM556T  |  Supply: 24 V  |  Microstep: 100 (20000 steps/rev)
 *
 * Pin mapping:
 *   PA8  (TIM1_CH1) -> STEP
 *   PA1              -> DIR
 */

#include "stepper.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

/* ── Device tree bindings ─────────────────────────────────────────────── */
#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

static const struct pwm_dt_spec step_pwm =
    PWM_DT_SPEC_GET(ZEPHYR_USER_NODE);
static const struct gpio_dt_spec dir_pin =
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, dir_gpios);

/* ── Derived timing ───────────────────────────────────────────────────── */
#define PERIOD_NS  ((uint32_t)((60.0f / TARGET_RPM / STEPS_PER_REV) * 1e9f))
#define PULSE_NS   (PERIOD_NS / 2u)

/* ── State ────────────────────────────────────────────────────────────── */
static long g_currentSteps = 0;

/* ── Public API ───────────────────────────────────────────────────────── */
int stepper_init(void)
{
    if (!pwm_is_ready_dt(&step_pwm)) {
        printk("STEP PWM not ready\n");
        return -1;
    }
    if (!gpio_is_ready_dt(&dir_pin)) {
        printk("DIR GPIO not ready\n");
        return -1;
    }
    int ret = gpio_pin_configure_dt(&dir_pin, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        printk("Failed to configure DIR pin\n");
        return ret;
    }
    printk("Stepper init OK — %.1f RPM, %lu steps/rev, %u ns period\n",
           (double)TARGET_RPM, STEPS_PER_REV, PERIOD_NS);
    return 0;
}

void moveToSteps(long targetSteps)
{
    long delta = targetSteps - g_currentSteps;
    if (delta == 0) return;

    gpio_pin_set_dt(&dir_pin, delta > 0 ? DIR_FORWARD : DIR_BACKWARD);
    k_msleep(1);   /* DIR settle */

    long steps = (delta > 0) ? delta : -delta;
    uint32_t wait_us = (uint32_t)(((uint64_t)steps * PERIOD_NS) / 1000u);

    pwm_set_dt(&step_pwm, PERIOD_NS, PULSE_NS);
    k_busy_wait(wait_us);
    pwm_set_dt(&step_pwm, PERIOD_NS, 0u);   /* stop PWM */

    g_currentSteps = targetSteps;
}

void moveToDegree(float deg)
{
    moveToSteps((long)((deg / 360.0f) * (float)STEPS_PER_REV));
}