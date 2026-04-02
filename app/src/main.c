#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

/* ── Devicetree handles ─────────────────────────────────────── */
#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

static const struct pwm_dt_spec step_pwm =
    PWM_DT_SPEC_GET(ZEPHYR_USER_NODE);                    /* PB0 TIM3_CH3 */
static const struct gpio_dt_spec dir_pin =
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, dir_gpios);        /* PB1 */

/* Motor encoder outputs from AM26LS32ACDR differential receiver */
static const struct gpio_dt_spec enc_a =
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, enc_a_gpios);      /* PA0 */
static const struct gpio_dt_spec enc_b =
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, enc_b_gpios);      /* PA1 */
static const struct gpio_dt_spec enc_z =
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, enc_z_gpios);      /* PA2 */

/*
 * DRV8452 is in H/W interface mode — no SPI, no firmware config.
 * ENABLE, nSLEEP hardwired high on PCB.
 * nFAULT not connected to MCU.
 * M0/M1/DECAY0/DECAY1 hardwired on PCB.
 */

/* ── Motion constants ───────────────────────────────────────── */
#define STEPS_PER_REV  400UL   /* 1/2 step × 200 full steps/rev */
#define TARGET_RPM     30.0f
#define PERIOD_NS      ((uint32_t)((60.0f / TARGET_RPM / STEPS_PER_REV) * 1e9f))
#define PULSE_NS       (PERIOD_NS / 2u)
#define DIR_FORWARD    1
#define DIR_BACKWARD   0

static long g_current_steps = 0;

/* ── Motion helpers ─────────────────────────────────────────── */
static long degrees_to_steps(float deg)
{
    return (long)((deg / 360.0f) * (float)STEPS_PER_REV);
}

static void move_to_steps(long target)
{
    long delta = target - g_current_steps;
    if (delta == 0) return;

    gpio_pin_set_dt(&dir_pin, delta > 0 ? DIR_FORWARD : DIR_BACKWARD);
    k_msleep(1); /* DIR setup: min 200 ns per datasheet, 1 ms is ample */

    long     steps   = (delta > 0) ? delta : -delta;
    uint32_t wait_us = (uint32_t)(((uint64_t)steps * PERIOD_NS) / 1000u);

    pwm_set_dt(&step_pwm, PERIOD_NS, PULSE_NS);
    k_busy_wait(wait_us);
    pwm_set_dt(&step_pwm, PERIOD_NS, 0u);

    g_current_steps = target;
}

static void move_to_degrees(float deg)
{
    move_to_steps(degrees_to_steps(deg));
}

/* ── Main ───────────────────────────────────────────────────── */
int main(void)
{
    if (!pwm_is_ready_dt(&step_pwm))  { printk("PWM not ready\n");  return -1; }
    if (!gpio_is_ready_dt(&dir_pin))  { printk("DIR not ready\n");  return -1; }
    if (!gpio_is_ready_dt(&enc_a))    { printk("ENC_A not ready\n"); return -1; }
    if (!gpio_is_ready_dt(&enc_b))    { printk("ENC_B not ready\n"); return -1; }
    if (!gpio_is_ready_dt(&enc_z))    { printk("ENC_Z not ready\n"); return -1; }

    gpio_pin_configure_dt(&dir_pin, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&enc_a,   GPIO_INPUT);
    gpio_pin_configure_dt(&enc_b,   GPIO_INPUT);
    gpio_pin_configure_dt(&enc_z,   GPIO_INPUT);

    printk("Stepper init OK (DRV8452 H/W mode)\n");
    printk("STEP : PB0 (TIM3_CH3)\n");
    printk("DIR  : PB1\n");
    printk("Steps/rev : %lu\n", STEPS_PER_REV);
    printk("Period    : %u ns (%u us)\n", PERIOD_NS, PERIOD_NS / 1000u);

    k_msleep(2000);

    float positions[] = { 0, 15, 30, 45, 60, 75, 90, 105, 120, 135, 150, 165, 180 };
    int   num_pos     = sizeof(positions) / sizeof(positions[0]);

    while (1) {
        for (int i = 0; i < num_pos; i++) {
            printk("-> %.0f deg  ENC A=%d B=%d Z=%d\n",
                   (double)positions[i],
                   gpio_pin_get_dt(&enc_a),
                   gpio_pin_get_dt(&enc_b),
                   gpio_pin_get_dt(&enc_z));
            move_to_degrees(positions[i]);
            k_msleep(500);
        }
        k_msleep(1000);
        for (int i = num_pos - 1; i >= 0; i--) {
            printk("-> %.0f deg  ENC A=%d B=%d Z=%d\n",
                   (double)positions[i],
                   gpio_pin_get_dt(&enc_a),
                   gpio_pin_get_dt(&enc_b),
                   gpio_pin_get_dt(&enc_z));
            move_to_degrees(positions[i]);
            k_msleep(500);
        }
        k_msleep(1000);
    }
    return 0;
}
