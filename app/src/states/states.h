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

/* Events */
typedef enum {
    EVT_NONE = 0,
    EVT_START_IDLE,
    EVT_START_HOMING,
    EVT_START_INITIALIZATION,
    EVT_START_BEND,
    EVT_START_COOL,
    EVT_START_COMPLETE,
    EVT_ERROR
} system_event_t;

extern struct k_msgq event_queue;
extern struct k_msgq display_queue;

static inline void event_post(system_event_t evt)
{
    k_msgq_put(&event_queue, &evt, K_NO_WAIT);
}

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

    /* Motor variables */
    float bend_angle;
} sm_ctx_t;

/* Global state machine context */
extern sm_ctx_t g_sm;

/* Functions */
const char* get_state_name(system_state_t state);   //get state name
void sm_transition(system_state_t new_state);   //transition to new state
void sm_init(void); //initialize state machine
void sm_run(void);  //run state machine loop

void idle_entry(void);
void homing_entry(void);
void initialization_entry(void);
void bend_entry(void);
void cool_entry(void);
void complete_entry(void);
void error_entry(void);

/* Display message */
typedef struct {
    system_state_t state;
} display_msg_t;
#endif  //STATE_H