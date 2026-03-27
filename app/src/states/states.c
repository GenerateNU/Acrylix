#include "states.h"
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

/* User inputs */
user_inputs_t g_inputs = {
    .bend_angle = 0,
    .thickness = 0,
    .bend_radii = 0
};

const char* get_state_name(system_state_t state)
{
    switch (state) {
        case STATE_IDLE:            return "IDLE";
        case STATE_HOMING:          return "HOMING";
        case STATE_INITIALIZATION:  return "INITIALIZATION";
        case STATE_BEND:            return "BEND";
        case STATE_COOL:            return "COOL";
        case STATE_COMPLETE:        return "COMPLETE";
        case STATE_ERROR:           return "ERROR";
        default:                    return "UNKNOWN";   
    }
}

void sm_transition(system_state_t new_state)
{
    if (new_state == g_sm.current) return;

    printk("State: %s -> %s \n", get_state_name(g_sm.current), (get_state_name(new_state)));

    g_sm.previous = g_sm.current;
    g_sm.current = new_state;

    /*Reset flags after each transition*/
    g_sm.temp_reached = false;
    g_sm.bend_in_place = false;
    g_sm.bend_complete = false;
    g_sm.cool_complete = false;

    /* Notify display thread when swtiching states*/
    display_msg_t dmsg = { .state = new_state };
    k_msgq_put(&display_queue, &dmsg, K_NO_WAIT);

    /* Run entry function*/
    switch (new_state) {
        case STATE_IDLE:            idle_entry();           break;
        case STATE_HOMING:          homing_entry();         break;
        case STATE_INITIALIZATION:  initialization_entry(); break;
        case STATE_BEND:            bend_entry();           break;
        case STATE_COOL:            cool_entry();           break;
        case STATE_COMPLETE:        complete_entry();       break;
        case STATE_ERROR:           error_entry();          break;
        default: break;
    }
}

void sm_init(void)
{
    g_sm.current = STATE_IDLE;
    g_sm.previous = STATE_IDLE;
    g_sm.error_code = ERROR_NONE;
    printk("State machine initialized. Current state: %s\n", get_state_name(g_sm.current));
}

/* State entry functions */
void idle_entry(void)
{
    printk("entering IDLE state \n");
}

void homing_entry(void)
{
    printk("entering HOMING state \n");
}

void initialization_entry(void)
{
    printk("entering INITIALIZATION state \n");
}

void bend_entry(void)
{
    printk("entering BEND state \n");
}

void cool_entry(void)
{
    printk("entering COOL state \n");
}

void complete_entry(void)
{
    printk("entering COMPLETE state \n");
}

void error_entry(void)
{
    printk("entering ERROR state \n");
}