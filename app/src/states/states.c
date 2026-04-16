/* states.c */
#include "states.h"
#include "stepper/stepper.h"
#include "stepper/limit_sw.h"
#include <zephyr/kernel.h>
#include "stepper/drv8452_spi.h"

/* ══════════════════════════════════════════════════════════════
 *  Global state machine context
 * ══════════════════════════════════════════════════════════════ */

sm_ctx_t g_sm = {
    .current       = STATE_IDLE,
    .previous      = STATE_IDLE,
    .error_code    = ERROR_NONE,
    .temp_reached  = false,
    .bend_in_place = false,
    .bend_complete = false,
    .cool_complete = false,
};

/* ══════════════════════════════════════════════════════════════
 *  User inputs (written by display, read by state thread)
 * ══════════════════════════════════════════════════════════════ */

user_inputs_t g_inputs = {
    .bend_angle = 0,
    .thickness  = 0,
    .bend_radii = 0,
};

/* ══════════════════════════════════════════════════════════════
 *  State name helper
 * ══════════════════════════════════════════════════════════════ */

const char *get_state_name(system_state_t state)
{
    switch (state) {
        case STATE_IDLE:           return "IDLE";
        case STATE_HOMING:         return "HOMING";
        case STATE_INITIALIZATION: return "INITIALIZATION";
        case STATE_BEND:           return "BEND";
        case STATE_COOL:           return "COOL";
        case STATE_COMPLETE:       return "COMPLETE";
        case STATE_ERROR:          return "ERROR";
        default:                   return "UNKNOWN";
    }
}

progress_update_t g_progress = { 0, 0, 0, false };

/* ══════════════════════════════════════════════════════════════
 *  State transition
 * ══════════════════════════════════════════════════════════════ */

void sm_transition(system_state_t new_state)
{
    if (new_state == g_sm.current) return;

    printk("State: %s -> %s\n",
           get_state_name(g_sm.current),
           get_state_name(new_state));

    g_sm.previous      = g_sm.current;
    g_sm.current       = new_state;
    g_sm.temp_reached  = false;
    g_sm.bend_in_place = false;
    g_sm.bend_complete = false;
    g_sm.cool_complete = false;

    /* Notify display thread */
    display_msg_t dmsg = { .state = new_state };
    k_msgq_put(&display_queue, &dmsg, K_NO_WAIT);

    /* Run entry function */
    switch (new_state) {
        case STATE_HOMING:         homing_entry();         break;
        case STATE_IDLE:           idle_entry();           break;
        case STATE_INITIALIZATION: initialization_entry(); break;
        case STATE_BEND:           bend_entry();           break;
        case STATE_COOL:           cool_entry();           break;
        case STATE_COMPLETE:       complete_entry();       break;
        case STATE_ERROR:          error_entry();          break;
        default: break;
    }
}

/* ══════════════════════════════════════════════════════════════
 *  State machine init
 * ══════════════════════════════════════════════════════════════ */

void sm_init(void)
{
    g_sm.current    = STATE_IDLE;
    g_sm.previous   = STATE_IDLE;
    g_sm.error_code = ERROR_NONE;
    printk("State machine initialized. Current state: %s\n",
           get_state_name(g_sm.current));
}

/* ══════════════════════════════════════════════════════════════
 *  State entry functions
 *  These run immediately on transition — keep them short.
 *  Heavy work (countdowns, motor control) lives in state_thread.
 * ══════════════════════════════════════════════════════════════ */

void idle_entry(void)
{
    printk("Entering IDLE\n");
}

void homing_entry(void)
{
    printk("Entering HOMING\n");
    //drv8452_enable();
    
    /* If already at home */
    if (limit_sw_is_pressed()){
        printk("Homing: already at home \n");
        stepper_reset_position();
        return;
    }

    printk("Homing: moving backward \n");
}

void initialization_entry(void)
{
    printk("Entering INITIALIZATION\n");
    /* Display thread handles input selection screens via display_queue */
}

void bend_entry(void)
{
    printk("Entering BEND — angle: %d deg, thickness: %s\n",
           g_inputs.bend_angle,
           g_inputs.thickness == 1 ? "1/8 in" : "1/16 in");
    drv8452_enable();

    if(limit_sw_is_pressed()){
        printk("Bend entry: limit switch pressed \n");
        g_sm.error_code = ERR_STEPPER;
    }

    if (limit_sw_is_pressed()){
        stepper_reset_position();
        sm_transition(STATE_IDLE);
    }

    printk("Bend entry: motor enable, heater on \n");
}

void cool_entry(void)
{
    printk("Entering COOL\n");
    /* State thread handles cooling countdown loop */
}

void complete_entry(void)
{
    printk("Entering COMPLETE\n");
    drv8452_disable();
    /* Display already updated via display_queue in sm_transition */
}

void error_entry(void)
{
    printk("Entering ERROR — code: %d\n", g_sm.error_code);
    drv8452_disable();    
}