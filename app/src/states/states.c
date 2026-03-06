#include "state.h"
#include <zephyr/kernel.h>

/* State machine context */
sm_ctx_t g_sm = {
    .current    = STATE_IDLE,
    .previous   = STATE_IDLE,
    .error_code = ERROR_NONE,

    /*completion flags*/
    .temp_reached = false,
    .bend_in_place = false,
    .bend_complete = false,
    .cool_complete = false,
};

const char* get_state_name(system_state_t state)
{
    switch (state) {
        case STATE_IDLE:            return "IDLE";
        case STATE_HOMING:          return "HOMING";
        case STATE_INTIALIZATION:   return "INTIALIZATION";
        case STATE_BEND:            return "BEND";
        case STATE_COOL:            return "COOL";
        case STATE_COMPLETE:        return "COMPLETE";
        case STATE_ERROR:           return "ERROR";
        default:                    return "UNKNOWN";   
    }
}

void sm_transition(system_state_t new_state)
{
    if (new_state == g_sm.current) {
        return; // No transition needed
    }

    printk("State: %s -> %s \n", get_state_name(g_sm.current), (get_state_name(new_state)));

    g_sm.previosu = g_sm.current;
    g_sm.current = new_state;

    /*Reset flags after each transition*/
    g_sm.temp_reached = false;
    g_sm.bend_in_place = false;
    g_sm.bend_complete = false;
    g_sm.cool_complete = false;
}

void sm_init(void)
{
    g_sm.current = STATE_IDLE;
    g_sm.previous = STATE_IDLE;
    g_sm.error_code = ERROR_NONE;
    printk("State machine initialized. Current state: %s\n", get_state_name(g_sm.current));
}