/* main.c */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include "states/states.h"
#include "display/display.h"
#include "stepper/stepper.h"
#include "stepper/limit_sw.h"
#include "stepper/drv8452_spi.h"
#include "temp/temp_control.h"
#include "app_events.h"

/* ══════════════════════════════════════════════════════════════
 *  Heating time constants
 * ══════════════════════════════════════════════════════════════ */

#define HEAT_TIME_1_16_MS   10000   /* 1/16 in — 10 s */
#define HEAT_TIME_1_8_MS     5000   /* 1/8 in  — 5 s  */
#define COOL_TIME_MS        10000   /* cooling duration */

/* ══════════════════════════════════════════════════════════════
 *  Bending constants
 * ══════════════════════════════════════════════════════════════ */
#define BEND_TIME_MS        10000   /* bending duration — adjust later */
#define BEND_RPM            10.0f   /* RPM during bending */
#define HOME_RPM            5.0f   /* RPM during homing — slow for accuracy */
#define HOME_MAX_DEG        360.0f  /* max degrees to travel when homing */

/* ══════════════════════════════════════════════════════════════
 *  Message queues (definitions — declared extern in states.h)
 * ══════════════════════════════════════════════════════════════ */

K_MSGQ_DEFINE(event_queue,   sizeof(system_event_t), 8, 4);
K_MSGQ_DEFINE(display_queue, sizeof(display_msg_t),  4, 4);

/* ══════════════════════════════════════════════════════════════
 *  Thread config
 * ══════════════════════════════════════════════════════════════ */

#define STATE_STACK_SIZE    8192
#define STATE_PRIORITY      7
#define DISPLAY_STACK_SIZE  20480   /* 20KB — debug optimizations inflate stack frames */
#define DISPLAY_PRIORITY    6

K_THREAD_STACK_DEFINE(state_stack,   STATE_STACK_SIZE);
K_THREAD_STACK_DEFINE(display_stack, DISPLAY_STACK_SIZE);
static struct k_thread state_thread_data;
static struct k_thread display_thread_data;

/* ══════════════════════════════════════════════════════════════
 *  Countdown helper — updates display progress every 250 ms
 * ══════════════════════════════════════════════════════════════ */

static void run_countdown(int total_ms)
{
    /* Wait until process_screen() has finished building all LVGL objects.
     * sm_transition() posts to display_queue and returns immediately, so
     * run_countdown() can be entered before the display thread has had a
     * chance to drain the queue and call process_screen().  Taking this
     * semaphore (given by process_screen at the end) ensures we never
     * write g_progress while bar/labels are still NULL. */
    k_sem_take(&display_screen_ready, K_MSEC(2000));

    int64_t start = k_uptime_get();
    while (1) {
        int64_t elapsed = k_uptime_get() - start;
        if (elapsed >= total_ms) break;

        int rem_ms = total_ms - (int)elapsed;

        /* Write to shared struct — display thread reads it */
        g_progress.pct = (int)((elapsed * 100) / total_ms);
        g_progress.min = rem_ms / 60000;
        g_progress.sec = (rem_ms % 60000) / 1000;
        __DMB();
        g_progress.pending = true;

        k_sleep(K_MSEC(250));
    }
    g_progress.pct = 100;
    g_progress.min = 0;
    g_progress.sec = 0;
    __DMB();
    g_progress.pending = true;
    k_sleep(K_MSEC(500));
}

/* ══════════════════════════════════════════════════════════════
 *  State machine thread
 * ══════════════════════════════════════════════════════════════ */

void state_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    size_t stack_free;
    k_thread_stack_space_get(k_current_get(), &stack_free);
    printk("State thread started, stack free: %zu\n", stack_free);

    while (1) {
        switch (g_sm.current) {

       /* ── HOMING ───────────────────────────────────────────
         * Placeholder — skip to IDLE for now.
         * ─────────────────────────────────────────────────── */
        case STATE_HOMING: {
            bool homing_error = false;
            float tot_moved = 0.0f;

            if (limit_sw_is_pressed()) {
                printk("Homing: already at home\n");
                stepper_reset_position();
                //stepper_move_degrees(2.0f, HOME_RPM);
                stepper_reset_position();
                sm_transition(STATE_INITIALIZATION);
                break;
            }

            printk("Homing: moving backward\n");
            while (!limit_sw_is_pressed()) {
                //stepper_move_degrees(-2.0f, HOME_RPM);
                tot_moved += 2.0f;
                printk("moving: %.1f deg\n", (double)tot_moved);

                if (tot_moved >= HOME_MAX_DEG) {
                    printk("Homing: limit switch not found after %.1f deg\n",
                           (double)tot_moved);
                    g_sm.error_code = ERROR_HOMING_FAILED;
                    sm_transition(STATE_ERROR);
                    homing_error = true;
                    break;
                }
            }

            if (!homing_error) {
                printk("Homing: home found after %.1f deg\n", (double)tot_moved);
                stepper_reset_position();
                //stepper_move_degrees(2.0f, HOME_RPM);
                stepper_reset_position();
                sm_transition(STATE_INITIALIZATION);
            }
            break;
        }


        /* ── IDLE ─────────────────────────────────────────────
         * Directions screen shown. Wait for forward button
         * to post EVT_START_INIT.
         * ─────────────────────────────────────────────────── */
        case STATE_IDLE: {
            system_event_t evt;
            if (k_msgq_get(&event_queue, &evt, K_MSEC(100)) == 0) {
                if (evt == EVT_START_INIT) {
                    sm_transition(STATE_INITIALIZATION);
                }
            }
            break;
        }

        /* ── INITIALIZATION ───────────────────────────────────
         * Input screens run on display thread.
         * Wait for EVT_START_BEND posted after thickness confirm.
         * ─────────────────────────────────────────────────── */
        case STATE_INITIALIZATION: {
            system_event_t evt;
            if (k_msgq_get(&event_queue, &evt, K_MSEC(100)) == 0) {
                if (evt == EVT_START_BEND) {
                    sm_transition(STATE_BEND);
                }
                /* User pressed back from first input step */
                if (evt == EVT_START_IDLE) {
                    sm_transition(STATE_IDLE);
                }
            }
            break;
        }

        /* ── BEND ─────────────────────────────────────────────
         * Phase 1: heat the acrylic (blocking sleep).
         * Phase 2: move motor to the user-selected bend angle.
         * Limit switch is the HOME sensor — not used here.
         * ─────────────────────────────────────────────────── */
        case STATE_BEND: {
            system_event_t dummy;
            while (k_msgq_get(&event_queue, &dummy, K_NO_WAIT) == 0) {}

            int heat_ms = (g_inputs.thickness == 1)
                          ? HEAT_TIME_1_8_MS
                          : HEAT_TIME_1_16_MS;

            printk("Heating: %d ms (thickness=%s)\n",
                   heat_ms,
                   g_inputs.thickness == 1 ? "1/8 in" : "1/16 in");
            k_sleep(K_MSEC(200));
            printk("Heating done\n");

            float target_deg = (float)g_inputs.bend_angle;
            printk("BEND: moving %.1f degrees at %.1f RPM\n",
                   (double)target_deg, (double)BEND_RPM);
            stepper_move_degrees(target_deg, BEND_RPM);
            printk("BEND: done. steps=%ld\n", stepper_get_steps());

            g_sm.bend_complete = true;
            sm_transition(STATE_COOL);
            break;
        }

        /* ── COOL ─────────────────────────────────────────────
         * Cooling countdown.
         * ─────────────────────────────────────────────────── */
        case STATE_COOL: {
            printk("Cooling: %d ms\n", COOL_TIME_MS);
            //run_countdown(COOL_TIME_MS);
            g_sm.cool_complete = true;
            sm_transition(STATE_COMPLETE);
            break;
        }

        /* ── COMPLETE ─────────────────────────────────────────
         * Show complete screen. Forward button posts EVT_START_IDLE
         * to restart.
         * ─────────────────────────────────────────────────── */
        case STATE_COMPLETE: {
            system_event_t evt;
            if (k_msgq_get(&event_queue, &evt, K_MSEC(100)) == 0) {
                if (evt == EVT_START_IDLE) {
                    sm_transition(STATE_IDLE);
                }
            }
            break;
        }


        /* ── ERROR ────────────────────────────────────────────
         * Hold error screen 10 s then return to IDLE.
         * ─────────────────────────────────────────────────── */
        case STATE_ERROR:
            k_sleep(K_SECONDS(10));
            sm_transition(STATE_IDLE);
            break;

        default:
            printk("Unknown state: %d\n", g_sm.current);
            break;
        }

        k_sleep(K_MSEC(10));
    }
}

/* ══════════════════════════════════════════════════════════════
 *  Display thread
 * ══════════════════════════════════════════════════════════════ */

void display_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    printk("Display thread started\n");

    if (display_init() != 0) {
        printk("Failed to initialize display\n");
        return;
    }

    /* display_init() already calls direction_screen() internally */

    size_t disp_stack_free;
    k_thread_stack_space_get(k_current_get(), &disp_stack_free);
    printk("Display thread: stack free after init: %zu / %d\n",
           disp_stack_free, DISPLAY_STACK_SIZE);

    display_msg_t dmsg;
    while (1) {
        while (k_msgq_get(&display_queue, &dmsg, K_NO_WAIT) == 0) {
            display_set_state(dmsg.state);
        }
        display_update();
        k_sleep(K_MSEC(10));
    }
}

/* ══════════════════════════════════════════════════════════════
 *  Main
 * ══════════════════════════════════════════════════════════════ */

int main(void)
{
    printk("=== Acrylix boot ===\n");
    k_msleep(100);

    limit_sw_init();

    if (drv8452_spi_init() != 0) {
        printk("drv8452_spi_init failed\n");
    }
    if (stepper_init() != 0) {
        printk("stepper_init failed\n");
    }

    sm_init();
    printk("sm_init done \n");

    //drv8452_disable();
    printk("creating state thread \n");
    k_tid_t state_tid = k_thread_create(
        &state_thread_data, state_stack,
        K_THREAD_STACK_SIZEOF(state_stack),
        state_thread, NULL, NULL, NULL,
        STATE_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(state_tid, "state");
    printk("State thread created\n");

    printk("creating display thread \n");
    k_tid_t display_tid = k_thread_create(
        &display_thread_data, display_stack,
        K_THREAD_STACK_SIZEOF(display_stack),
        display_thread, NULL, NULL, NULL,
        DISPLAY_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(display_tid, "display");
    printk("Display thread created\n");

    // temp_init();
    // heater_start();

    // /* SSR toggle test — PC9 high/low every 2 seconds */
    // const struct device *gpioc = DEVICE_DT_GET(DT_NODELABEL(gpioc));
    // gpio_pin_configure(gpioc, 9, GPIO_OUTPUT_INACTIVE);

    // while (1) {
    //     gpio_pin_set(gpioc, 9, 1);
    //     printk("SSR ON\n");
    //     k_sleep(K_SECONDS(2));

    //     gpio_pin_set(gpioc, 9, 0);
    //     printk("SSR OFF\n");
    //     k_sleep(K_SECONDS(2));
    // }

    k_sleep(K_FOREVER);
    return 0;
}