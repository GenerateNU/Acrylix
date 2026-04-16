/* main.c */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/drivers/hwinfo.h>
#include "states/states.h"
#include "display/display.h"
#include "stepper/stepper.h"
#include "stepper/limit_sw.h"
#include "stepper/drv8452_spi.h"
#include "temp/temp_control.h"
#include "temp/temp_sensor.h"
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
         * Enable driver, drive backward until limit switch ISR
         * fires (sets g_triggered flag + calls emergency_stop to
         * unblock the move), then reset position.
         * ─────────────────────────────────────────────────── */
        case STATE_HOMING: {
            drv8452_enable();
            limit_sw_clear_trigger();

            if (limit_sw_is_pressed()) {
                printk("Homing: already at home\n");
                stepper_reset_position();
                drv8452_disable();
                sm_transition(STATE_IDLE);
                break;
            }

            printk("Homing: moving backward %.0f deg at %.1f RPM\n",
                   (double)HOME_MAX_DEG, (double)HOME_RPM);
            stepper_move_degrees(-HOME_MAX_DEG, HOME_RPM);

            if (limit_sw_triggered()) {
                printk("Homing: home found\n");
                stepper_reset_position();
                drv8452_disable();
                sm_transition(STATE_IDLE);
            } else {
                printk("Homing: limit switch not found — ERROR\n");
                g_sm.error_code = ERROR_HOMING_FAILED;
                drv8452_disable();
                sm_transition(STATE_ERROR);
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
                if (evt == EVT_START_HEAT) {
                    sm_transition(STATE_HEAT);
                }
                /* User pressed back from first input step */
                if (evt == EVT_START_IDLE) {
                    sm_transition(STATE_IDLE);
                }
            }
            break;
        }

        /* ── HEAT ─────────────────────────────────────────────
         * Turn on heating element to correct temperature.
         * Start timer to heat acrylic.
         * ─────────────────────────────────────────────────── */

        case STATE_HEAT: {
            #define TARGET_TEMP         150.0f   //make 150
            #define WARMUP_TIMEOUT_MS   120000

            printk("HEAT: starting heater, target=%.1f C\n", (double)TARGET_TEMP);
            heater_start();

            bool heat_error = false;
            int64_t warmup_start = k_uptime_get();

            /* Phase 1 — warmup: wait for target temperature */
            while (temp_get_ema() < TARGET_TEMP) {
                display_post_heat_temp(temp_get_ema());
                printk("HEAT: current=%.1f C\n", (double)temp_get_ema());
                k_sleep(K_MSEC(500));
                if (k_uptime_get() - warmup_start > WARMUP_TIMEOUT_MS) {
                    printk("HEAT: warmup timeout\n");
                    heat_error = true;
                    break;
                }
            }

            if (heat_error) {
                heater_stop();
                g_sm.error_code = ERROR_HEATER_TEMP;
                sm_transition(STATE_ERROR);
                break;
            }

            printk("HEAT: target temperature reached\n");

            /* Phase 2 — soak: hold at temperature for thickness-dependent time */
            int heat_ms = (g_inputs.thickness == 1)
                          ? HEAT_TIME_1_8_MS
                          : HEAT_TIME_1_16_MS;

            printk("HEAT: soaking %d ms (thickness=%s)\n",
                   heat_ms,
                   g_inputs.thickness == 1 ? "1/8 in" : "1/16 in");

            k_sem_take(&display_screen_ready, K_MSEC(2000));
            int64_t soak_start = k_uptime_get();
            while (1) {
                int64_t elapsed = k_uptime_get() - soak_start;
                if (elapsed >= heat_ms) break;
                int rem_ms = heat_ms - (int)elapsed;
                g_progress.pct = (int)((elapsed * 100) / heat_ms);
                g_progress.min = rem_ms / 60000;
                g_progress.sec = (rem_ms % 60000) / 1000;
                __DMB();
                g_progress.pending = true;
                k_sleep(K_MSEC(500));
            }
            g_progress.pct = 100;
            g_progress.min = 0;
            g_progress.sec = 0;
            __DMB();
            g_progress.pending = true;
            k_sleep(K_MSEC(300));

            heater_stop();
            printk("HEAT: done\n");
            sm_transition(STATE_BEND);
            break;
        }

        /* ── BEND ─────────────────────────────────────────────
         * Move motor to the user-selected bend angle.
         * Limit switch is the HOME sensor — not used here.
         * ─────────────────────────────────────────────────── */
        case STATE_BEND: {

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

            k_sem_take(&display_screen_ready, K_MSEC(2000));
            int64_t cool_start = k_uptime_get();
            while (1) {
                int64_t elapsed = k_uptime_get() - cool_start;
                if (elapsed >= COOL_TIME_MS) break;
                int rem_ms = COOL_TIME_MS - (int)elapsed;
                g_progress.pct = (int)((elapsed * 100) / COOL_TIME_MS);
                g_progress.min = rem_ms / 60000;
                g_progress.sec = (rem_ms % 60000) / 1000;
                __DMB();
                g_progress.pending = true;
                k_sleep(K_MSEC(500));
            }
            g_progress.pct = 100;
            g_progress.min = 0;
            g_progress.sec = 0;
            __DMB();
            g_progress.pending = true;
            k_sleep(K_MSEC(300));

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
            k_sleep(K_MSEC(500));
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
        printk("Display init failed — rebooting\n");
        k_msleep(100);
        sys_reboot(SYS_REBOOT_COLD);
    }

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

    uint32_t reset_cause = 0;
    hwinfo_get_reset_cause(&reset_cause);
    hwinfo_clear_reset_cause();
    if (reset_cause & RESET_POR) {
        printk("Cold boot (POR) — rebooting for display stabilization\n");
        k_msleep(200);
        sys_reboot(SYS_REBOOT_COLD);
    }

    k_msleep(1000);

    /* Hardware init */
    limit_sw_init();
    temp_init();

    if (drv8452_spi_init() != 0) {
        printk("drv8452_spi_init failed\n");
    }
    if (stepper_init() != 0) {
        printk("stepper_init failed\n");
    }

    /*const struct device *gpioc_dev = DEVICE_DT_GET(DT_NODELABEL(gpioc));
    gpio_pin_configure(gpioc_dev, 7, GPIO_OUTPUT_ACTIVE);
    k_msleep(10);
    gpio_pin_set(gpioc_dev, 7, 0);  
    k_msleep(20);
    gpio_pin_set(gpioc_dev, 7, 1);
    k_msleep(150);    */              

    sm_init();
    printk("sm_init done \n");

    //sys_reboot(SYS_REBOOT_COLD);

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