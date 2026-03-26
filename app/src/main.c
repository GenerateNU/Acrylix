/*
 * stepper.c — NEMA 23 via DM556T, interrupt-driven step counting
 *
 * Motor : 23HS22-4004-ME1K
 * Driver: DM556T  |  Supply: 24 V  |  Microstep: 100 (20 000 steps/rev)
 *
 * How it works:
 *   - PWM runs continuously on TIM1_CH1 (PA8) while moving
 *   - A GPIO interrupt on the STEP pin counts each pulse
 *   - When the target count is reached the ISR kills the PWM and posts
 *     a semaphore so the calling thread can block (yields CPU) instead
 *     of busy-waiting
 *
 * Overlay additions needed (zephyr_user node):
 *   pwms       = <&timers1 1 PERIOD_NS PWM_POLARITY_NORMAL>;
 *   dir-gpios  = <&gpiob 10 GPIO_ACTIVE_HIGH>;
 *   step-gpios = <&gpioa 8  GPIO_ACTIVE_HIGH>;   // feedback pin for counting
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include "stepper.h"

/* ── Device tree ──────────────────────────────────────────────────────────── */
#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

static const struct pwm_dt_spec  step_pwm  = PWM_DT_SPEC_GET(ZEPHYR_USER_NODE);
static const struct gpio_dt_spec dir_pin   = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, dir_gpios);
static const struct gpio_dt_spec step_fb   = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, step_gpios);

/* ── Motor config ─────────────────────────────────────────────────────────── */
#define STEPS_PER_REV   20000UL
#define TARGET_RPM      30.0f
#define PERIOD_NS       ((uint32_t)((60.0f / TARGET_RPM / STEPS_PER_REV) * 1e9f))
#define PULSE_NS        (PERIOD_NS / 2u)

#define DIR_FORWARD     1
#define DIR_BACKWARD    0

/* ── ISR state (shared between ISR and thread — volatile + atomic) ────────── */
static volatile long        g_target_steps = 0;   /* steps remaining */
static struct k_sem         g_move_done;
static struct gpio_callback g_step_cb;

/* ── Position tracking ────────────────────────────────────────────────────── */
static long g_current_steps = 0;

/* ── Step ISR ─────────────────────────────────────────────────────────────── */
static void step_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(dev); ARG_UNUSED(cb); ARG_UNUSED(pins);

    g_target_steps--;

    if (g_target_steps <= 0) {
        /* Stop PWM — 0 pulse width = output held low */
        pwm_set_dt(&step_pwm, PERIOD_NS, 0u);
        k_sem_give(&g_move_done);   /* wake the waiting thread */
    }
}

/* ── Init ─────────────────────────────────────────────────────────────────── */
int stepper_init(void)
{
    if (!pwm_is_ready_dt(&step_pwm)) {
        printk("STEP PWM not ready\n");
        return -ENODEV;
    }
    if (!gpio_is_ready_dt(&dir_pin) || !gpio_is_ready_dt(&step_fb)) {
        printk("Stepper GPIO not ready\n");
        return -ENODEV;
    }

    k_sem_init(&g_move_done, 0, 1);

    /* DIR pin: output, default forward */
    gpio_pin_configure_dt(&dir_pin, GPIO_OUTPUT_INACTIVE);

    /* STEP feedback pin: input with interrupt on rising edge */
    gpio_pin_configure_dt(&step_fb, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&step_fb, GPIO_INT_EDGE_RISING);
    gpio_init_callback(&g_step_cb, step_isr, BIT(step_fb.pin));
    gpio_add_callback(step_fb.port, &g_step_cb);

    printk("Stepper init OK — %u ns period (%.1f RPM)\n",
           PERIOD_NS, (double)TARGET_RPM);
    return 0;
}

/* ── Move (non-blocking to the system — calling thread blocks on sem) ─────── */
static void move_to_steps(long target)
{
    long delta = target - g_current_steps;
    if (delta == 0) return;

    gpio_pin_set_dt(&dir_pin, delta > 0 ? DIR_FORWARD : DIR_BACKWARD);
    k_msleep(1);   /* DIR settle */

    g_target_steps = (delta > 0) ? delta : -delta;

    /* Reset semaphore then start PWM */
    k_sem_reset(&g_move_done);
    pwm_set_dt(&step_pwm, PERIOD_NS, PULSE_NS);

    /* Block THIS thread only — all other threads keep running */
    k_sem_take(&g_move_done, K_FOREVER);

    g_current_steps = target;
}

/* ── Public API ───────────────────────────────────────────────────────────── */
void stepper_move_to_degrees(float deg)
{
    long steps = (long)((deg / 360.0f) * (float)STEPS_PER_REV);
    move_to_steps(steps);
}

long stepper_get_steps(void)      { return g_current_steps; }
float stepper_get_degrees(void)   { return (g_current_steps * 360.0f) / STEPS_PER_REV; }
