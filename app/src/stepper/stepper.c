/*
 * stepper.c — NEMA 23 via DRV8452, interrupt-driven step counting
 *
 * Motor : 23HS22-4004-ME1K  (1.8°/step = 200 full steps/rev)
 * Driver: DRV8452  |  Supply: 24 V  |  Microstep: 1/8 (CTRL2=0x06, confirmed by step count)
 *         → 1600 steps/rev
 *
 * How it works:
 *   - PWM runs continuously on TIM1_CH1 (PA8) while moving
 *   - A GPIO interrupt on the STEP pin counts each pulse
 *   - When the target count is reached the ISR kills the PWM and posts
 *     a semaphore so the calling thread can block (yields CPU) instead
 *     of busy-waiting
 *   - If the feedback pin is not wired, moves complete via open-loop timeout
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
#define FULL_STEPS_PER_REV  200UL
#define MICROSTEPS          8UL     /* DRV8452 CTRL2=0x06 = 1/8 microstep (confirmed by step count) */
#define STEPS_PER_REV       (FULL_STEPS_PER_REV * MICROSTEPS)  /* 6400 */

#define DIR_FORWARD     1
#define DIR_BACKWARD    0

/* ── ISR state (shared between ISR and thread — volatile + atomic) ────────── */
static volatile long        g_target_steps = 0;   /* steps remaining */
static volatile long g_total_steps = 0;
static struct k_sem         g_move_done;
static struct gpio_callback g_step_cb;

/* ── Position tracking ────────────────────────────────────────────────────── */
static long g_current_steps = 0;

/* ── Step ISR ─────────────────────────────────────────────────────────────── */
static void step_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(dev); ARG_UNUSED(cb); ARG_UNUSED(pins);
    printk("stepISR fired \n");
    
    g_target_steps--;

    if (g_target_steps <= 0) {
        /* Stop PWM — 0 pulse width = output held low */
        pwm_set_dt(&step_pwm, 312500u, 0u);
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
    gpio_pin_interrupt_configure_dt(&step_fb, GPIO_INT_EDGE_RISING);
    gpio_init_callback(&g_step_cb, step_isr, BIT(step_fb.pin));
    gpio_add_callback(step_fb.port, &g_step_cb);

    printk("Stepper init OK — %lu steps/rev (1/%lu microstep)\n",
           STEPS_PER_REV, MICROSTEPS);
    return 0;
}

/* ── Internal: execute a relative move with a given step period ───────────── */
static void do_move(long steps, uint32_t period_ns, int dir)
{
    uint32_t pulse_ns = period_ns / 2u;
    printk("do_move: steps=%ld period_ns=%u\n", steps, period_ns);
    
    gpio_pin_set_dt(&dir_pin, dir);
    k_msleep(1);   /* DIR settle */

    g_target_steps = steps;
    g_total_steps = steps;

    /* Open-loop timeout: expected move time + 5 ms buffer */
    uint32_t timeout_ms = (uint32_t)(((uint64_t)steps * period_ns) / 1000000ULL) + 5U;

    k_sem_reset(&g_move_done);
    int ret = pwm_set_dt(&step_pwm, period_ns, pulse_ns);
    if (ret != 0) {
        printk("stepper: pwm_set_dt failed: %d\n", ret);
        return;
    }

    if (k_sem_take(&g_move_done, K_MSEC(timeout_ms)) != 0) {
        pwm_set_dt(&step_pwm, period_ns, 0u);
        printk("stepper: open-loop timeout after %u ms\n", timeout_ms);
    }
}

/* ── Emergency stop ───────────── */
void stepper_emergency_stop(void)
{
    pwm_set_dt(&step_pwm, 312500u, 0u);
    g_target_steps = 0;
    k_sem_give(&g_move_done);
    printk("Stepper: emergencyt stop \n");
}

/* ── Homing of motor ───────────── */
void stepper_reset_position(void)
{
    g_current_steps = 0;
    printk("Stepper: position reset to 0\n");
}

/* ── Gets progress of stepper for the UI ───────────── */
float stepper_get_progress(void)
{
    if (g_total_steps <= 0) return 1.0f;
    long done = g_total_steps - g_target_steps;
    if (done < 0) done = 0;
    float pct = (float)done / (float)g_total_steps;
    return pct > 1.0f ? 1.0f : pct;
}
/* ── Public API ───────────────────────────────────────────────────────────── */

/**
 * Relative move by degrees at a given RPM.
 * Positive degrees = forward (DIR high), negative = backward (DIR low).
 * Blocks until the move completes (step ISR) or times out (open-loop).
 */
void stepper_move_degrees(float degrees, float rpm)
{
    //printk("start moving \n");
    if (degrees == 0.0f) return;

    float abs_deg = degrees > 0.0f ? degrees : -degrees;
    long  steps   = (long)((abs_deg / 360.0f) * (float)STEPS_PER_REV);
    if (steps == 0) return;

    /* step_period_ns = (60 / (rpm * steps_per_rev)) * 1e9 */
    uint32_t period_ns = (uint32_t)((60.0f / (rpm * (float)STEPS_PER_REV)) * 1e9f);
    if (period_ns < 5000U)     period_ns = 5000U;     /* 200 kHz max step rate */
    if (period_ns > 50000000U) period_ns = 50000000U; /* 20 Hz min step rate (~1.5 RPM) */

    int dir = (degrees > 0.0f) ? DIR_FORWARD : DIR_BACKWARD;
    do_move(steps, period_ns, dir);

    g_current_steps += (degrees > 0.0f) ? steps : -steps;
    //printk("finish moving degrees \n");
}

long  stepper_get_steps(void)   { return g_current_steps; }
float stepper_get_degrees(void) { return (g_current_steps * 360.0f) / (float)STEPS_PER_REV; }
