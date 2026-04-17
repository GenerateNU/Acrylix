#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include "stepper.h"

/* ── Device tree ──────────────────────────────────────────────────────────── */
#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

static const struct pwm_dt_spec  step_pwm = PWM_DT_SPEC_GET(ZEPHYR_USER_NODE);
static const struct gpio_dt_spec dir_pin  = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, dir_gpios);

/* ── Motor config ─────────────────────────────────────────────────────────── */
#define FULL_STEPS_PER_REV  200UL
#define MICROSTEPS          1UL     /* DRV8452 CTRL2=0x00 = full step mode */
#define GEAR_RATIO          10.0f   /* tune: commanded_deg / actual_deg × current value */
#define STEPS_PER_REV_F     ((float)FULL_STEPS_PER_REV * (float)MICROSTEPS * GEAR_RATIO)
#define STEP_PERIOD_NS_MAX  20000000U   /* ~0.03 RPM at motor output with 10:1 gearbox */

#define DIR_FORWARD     1
#define DIR_BACKWARD    0

/* ── Move state ───────────────────────────────────────────────────────────── */
static volatile long g_total_steps = 0;
static struct k_sem  g_move_done;

/* ── Position tracking ────────────────────────────────────────────────────── */
static long g_current_steps = 0;

/* ── Init ─────────────────────────────────────────────────────────────────── */
int stepper_init(void)
{
    if (!pwm_is_ready_dt(&step_pwm)) {
        printk("STEP PWM not ready\n");
        return -ENODEV;
    }
    if (!gpio_is_ready_dt(&dir_pin)) {
        printk("Stepper DIR GPIO not ready\n");
        return -ENODEV;
    }

    k_sem_init(&g_move_done, 0, 1);

    /* DIR pin: output, default low (backward) until first move */
    gpio_pin_configure_dt(&dir_pin, GPIO_OUTPUT_INACTIVE);

    printk("Stepper init OK — %.1f steps/rev (gear %.1f)\n",
           (double)STEPS_PER_REV_F, (double)GEAR_RATIO);
    return 0;
}

/* ── Internal: execute a relative move with a given step period ───────────── */
static void do_move(long steps, uint32_t period_ns, int dir)
{
    uint32_t pulse_ns = period_ns / 2u;

    gpio_pin_set_dt(&dir_pin, dir);
    k_msleep(1);   /* DIR settle */

    g_total_steps = steps;

    /* Open-loop: run for exactly the expected move duration + 200 ms margin */
    uint32_t timeout_ms = (uint32_t)(((uint64_t)steps * period_ns) / 1000000ULL) + 200U;

    k_sem_reset(&g_move_done);
    pwm_set_dt(&step_pwm, period_ns, pulse_ns);

    if (k_sem_take(&g_move_done, K_MSEC(timeout_ms)) != 0) {
        pwm_set_dt(&step_pwm, period_ns, 0u);
        printk("stepper: open-loop timeout after %u ms\n", timeout_ms);
    }
}

/* ── Emergency stop ───────────── */
void stepper_emergency_stop(void)
{
    pwm_set_dt(&step_pwm, 312500u, 0u);
    k_sem_give(&g_move_done);
    printk("Stepper: emergency stop\n");
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
    /* Step ISR removed — progress not available in open-loop mode */
    return 0.0f;
}
/* ── Public API ───────────────────────────────────────────────────────────── */

/**
 * Relative move by degrees at a given RPM.
 * Positive degrees = forward (DIR high), negative = backward (DIR low).
 * Blocks until the move completes (step ISR) or times out (open-loop).
 */
void stepper_move_degrees(float degrees, float rpm)
{
    if (degrees == 0.0f) return;

    float abs_deg = degrees > 0.0f ? degrees : -degrees;
    long  steps   = (long)((abs_deg / 360.0f) * (float)STEPS_PER_REV_F);
    if (steps == 0) return;

    /* step_period_ns = (60 / (rpm * steps_per_rev)) * 1e9 */
    uint32_t period_ns = (uint32_t)((60.0f / (rpm * (float)STEPS_PER_REV_F)) * 1e9f);
    if (period_ns < 31250U)   period_ns = 31250U;   /* 2 counts min @ 64 kHz */
    if (period_ns > STEP_PERIOD_NS_MAX) period_ns = STEP_PERIOD_NS_MAX;
    int dir = (degrees > 0.0f) ? DIR_FORWARD : DIR_BACKWARD;
    do_move(steps, period_ns, dir);

    g_current_steps += (degrees > 0.0f) ? steps : -steps;
}

long  stepper_get_steps(void)   { return g_current_steps; }
float stepper_get_degrees(void) { return (g_current_steps * 360.0f) / (float)STEPS_PER_REV_F; }