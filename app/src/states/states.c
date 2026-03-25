#include "states.h"
#include <zephyr/kernel.h>

/* Message queues */
K_MSGQ_DEFINE(event_queue, sizeof(system_event_t), 10, 4);
K_MSGQ_DEFINE(display_queue, sizeof(display_msg_t), 10, 4);

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

void sm_run(void)
{
    system_event_t evt;
    while (1) {
        k_msgq_get(&event_queue, &evt, K_FOREVER);
        switch (evt) {
            case EVT_START_IDLE:            sm_transition(STATE_IDLE);              break;
            case EVT_START_INITIALIZATION:  sm_transition(STATE_INITIALIZATION);    break;
            case EVT_START_BEND:            sm_transition(STATE_BEND);              break;
            case EVT_START_COOL:            sm_transition(STATE_COOL);              break;
            case EVT_START_COMPLETE:        sm_transition(STATE_COMPLETE);          break;
            case EVT_START_HOMING:          sm_transition(STATE_HOMING);            break;
            case EVT_ERROR:                 sm_transition(STATE_ERROR);             break;
            case EVT_NONE:                  break;
            default:                break;
        }
    }
}

void sm_init(void)
{
    g_sm.current = STATE_ERROR;
    g_sm.previous = STATE_IDLE;
    g_sm.error_code = ERROR_NONE;
    printk("State machine initialized. Current state: %s\n", get_state_name(g_sm.current));
}

/* State entry functions */
void idle_entry(void)
{
    printk("entering IDLE state \n");
    k_sleep(K_SECONDS(5));  //replace with actual code later

    printk("IDLE complete \n");
    event_post(EVT_START_INITIALIZATION);
}

void homing_entry(void)
{
    printk("entering HOMING state \n");
    k_sleep(K_SECONDS(5));

    printk("HOMING complete \n");
    event_post(EVT_START_IDLE);
}

void initialization_entry(void)
{
    printk("entering INITIALIZATION state \n");
    k_sleep(K_SECONDS(5));

    printk("INITIALIZATION complete \n");
    event_post(EVT_START_BEND);
}

void bend_entry(void)
{
    printk("entering BEND state \n");
    k_sleep(K_SECONDS(5));

    printk("BEND complete \n");
    event_post(EVT_START_COOL);
}

void cool_entry(void)
{
    printk("entering COOL state \n");
    k_sleep(K_SECONDS(5));

    printk("Cool complete \n");
    event_post(EVT_START_COMPLETE);
}

void complete_entry(void)
{
    printk("entering COMPLETE state \n");
    k_sleep(K_SECONDS(5));

    printk("Completed \n");
    event_post(EVT_START_IDLE);

}

void error_entry(void)
{
    printk("entering ERROR state \n");
    k_sleep(K_SECONDS(5));

    event_post(EVT_START_IDLE);
}