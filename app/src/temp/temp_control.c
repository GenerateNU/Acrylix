#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "temp_control.h"
#include "temp_sensor.h"
#include "../ssr.h"

LOG_MODULE_REGISTER(temp_control, LOG_LEVEL_INF);

/* ── PI controller ────────────────────────────────────────────────────────── */
#define KP           0.5
#define KI           0.1
#define TS           1.0      /* seconds per cycle — matches 1 Hz loop rate */
#define MAX_OUTPUT   100
#define MIN_OUTPUT   0
#define INTEGRAL_MAX 500
#define INTEGRAL_MIN -500

static double integral_term = 0.0;
static double setpoint      = 150.0;

static double update_pi(double input_value)
{
    double error = setpoint - input_value;

    integral_term += error * TS;

    if (integral_term > INTEGRAL_MAX) integral_term = INTEGRAL_MAX;
    else if (integral_term < INTEGRAL_MIN) integral_term = INTEGRAL_MIN;

    double output = (KP * error) + (KI * integral_term);

    if (output > MAX_OUTPUT) output = MAX_OUTPUT;
    else if (output < MIN_OUTPUT) output = MIN_OUTPUT;

    return output;
}

/* ── Heater thread ────────────────────────────────────────────────────────── */
static K_THREAD_STACK_DEFINE(heater_stack, 1024);
static struct k_thread heater_thread_data;
static k_tid_t heater_tid;

static void heater_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

    /* Wait for ADC thread to produce a valid EMA reading before starting PI */
    while (temp_get_ema() < -900.0f) {
        k_sleep(K_MSEC(100));
    }
    LOG_INF("EMA valid — starting PI control");

    while (1) {
        float measured  = temp_get_ema();
        double pi_out   = update_pi((double)measured);
        bool heater_on  = (pi_out > 0.5);

        ssr_set(heater_on);
        LOG_INF("T=%.1f setpoint=%.1f PI=%.1f SSR=%s",
                (double)measured, setpoint, pi_out,
                heater_on ? "ON" : "OFF");

        k_sleep(K_SECONDS(1));
    }
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void heater_start(void)
{
    integral_term = 0.0;  /* reset integrator to prevent windup on restart */
    k_thread_resume(heater_tid);
    LOG_INF("Heater started (setpoint=%.1f C)", setpoint);
}

void heater_stop(void)
{
    k_thread_suspend(heater_tid);
    ssr_set(false);
    LOG_INF("Heater stopped");
}

void temp_init(void)
{
    temp_thread_init();
    ssr_init();

    heater_tid = k_thread_create(&heater_thread_data, heater_stack,
                                 K_THREAD_STACK_SIZEOF(heater_stack),
                                 heater_thread, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    k_thread_name_set(heater_tid, "heater");
    k_thread_suspend(heater_tid);  /* starts suspended — call heater_start() to begin */
}
