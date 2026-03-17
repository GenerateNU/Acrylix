#ifndef STATE_H
#define STATE_H

#include <zephyr/kernel.h>
#include <stdbool.h>

/* System states */
typedef enum {
    STATE_IDLE,
    STATE_HOMING,
    STATE_INITIALIZATION,
    STATE_BEND,
    STATE_COOL,
    STATE_COMPLETE,
    STATE_ERROR
} system_state_t;

/* Error codes */
typedef enum {
    ERROR_NONE,
    ERROR_HOMING_FAILED,
    ERROR_HEATER_TEMP,  //Heater did not reach target temp
    ERR_STEPPER,    //Wrong rotation count
} error_code_t;

/* State machine context */
typedef struct {
    system_state_t current;
    system_state_t previous;
    error_code_t error_code;

    /* Completion flags */
    bool temp_reached;
    bool bend_in_place;
    bool bend_complete;
    bool cool_complete;
} sm_ctx_t;

/* Global state machine context */
extern sm_ctx_t g_sm;

/* Functions */
const char* get_state_name(system_state_t state);   //get state name
void sm_transition(system_state_t new_state);   //transition to new state
void sm_init(void); //initialize state machine

#endif  //STATE_H