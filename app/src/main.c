/*
 * NEMA 23 stepper via DM556T — 0→180→0 sweep test
 *
 * Motor : 23HS22-4004-ME1K
 * Driver: DM556T  |  Supply: 24 V  |  Microstep: 100 (20000 steps/rev)
 *
 * Pin mapping (defined in boards/nucleo_f446re.overlay):
 *   PA8  (TIM1_CH1) -> STEP / PUL-   (alias: stepper-step)
 *   PB10             -> DIR-           (alias: stepper-dir)
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

/* ── Device tree bindings ─────────────────────────────────────────────────── */
#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

static const struct pwm_dt_spec  step_pwm =
    PWM_DT_SPEC_GET(ZEPHYR_USER_NODE);
static const struct gpio_dt_spec dir_pin  =
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, dir_gpios);

/* ── Motor config ─────────────────────────────────────────────────────────── */
#define STEPS_PER_REV  400UL
#define TARGET_RPM     30.0f

/*
 * Step period in nanoseconds:
 * period_ns = (60 / 30 / 400) * 1e9 = 5,000,000 ns = 5 ms per step = 200 Hz
 */
#define PERIOD_NS  ((uint32_t)((60.0f / TARGET_RPM / STEPS_PER_REV) * 1e9f))
#define PULSE_NS   (PERIOD_NS / 2u)   /* 50% duty cycle */

#define DIR_FORWARD  1
#define DIR_BACKWARD 0

/* ── State ────────────────────────────────────────────────────────────────── */
static long g_currentSteps = 0;

/* ── Helpers ──────────────────────────────────────────────────────────────── */
static long degreesToSteps(float deg)
{
    return (long)((deg / 360.0f) * (float)STEPS_PER_REV);
}

static void moveToSteps(long targetSteps)
{
    long delta = targetSteps - g_currentSteps;
    if (delta == 0) return;

    /* Set direction */
    gpio_pin_set_dt(&dir_pin, delta > 0 ? DIR_FORWARD : DIR_BACKWARD);
    k_msleep(1);   /* DIR settle — conservative but safe */

    long steps    = (delta > 0) ? delta : -delta;
    uint32_t wait_us = (uint32_t)(((uint64_t)steps * PERIOD_NS) / 1000u);

    /* Start PWM, busy-wait for all pulses, then stop */
    pwm_set_dt(&step_pwm, PERIOD_NS, PULSE_NS);
    k_busy_wait(wait_us);
    pwm_set_dt(&step_pwm, PERIOD_NS, 0u);   /* 0 pulse width = output low */

    g_currentSteps = targetSteps;
}

static void moveToDegree(float deg)
{
    moveToSteps(degreesToSteps(deg));
}

/* ── main ─────────────────────────────────────────────────────────────────── */
int main(void)
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

    printk("Stepper init OK\n");
    printk("RPM:        %.1f\n", (double)TARGET_RPM);
    printk("Steps/rev:  %lu\n",  STEPS_PER_REV);
    printk("Period:     %u ns (%u us)\n", PERIOD_NS, PERIOD_NS / 1000u);

    k_msleep(2000);   /* 2 s startup pause */

    float positions[] = { 0, 15, 30, 45, 60, 75, 90, 105, 120, 135, 150, 165, 180 };
    int   numPos      = sizeof(positions) / sizeof(positions[0]);

    while (1) {
        /* Forward: 0 → 180 */
        for (int i = 0; i < numPos; i++) {
            moveToDegree(positions[i]);
            k_msleep(500);
        }

        k_msleep(1000);

        /* Reverse: 180 → 0 */
        for (int i = numPos - 1; i >= 0; i--) {
            moveToDegree(positions[i]);
            k_msleep(500);
        }

        k_msleep(1000);
    }

    return 0;
}
