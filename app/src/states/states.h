#ifndef STATE_H
#define STATE_H

#include <zephyr/kernel.h>
#include <stdbool.h>

/* ── System states ── */
typedef enum {
    STATE_IDLE,
    STATE_HOMING,
    STATE_INITIALIZATION,
    STATE_BEND,
    STATE_COOL,
    STATE_COMPLETE,
    STATE_ERROR
} system_state_t;

/* ── Events ── */
typedef enum {
    EVT_NONE = 0,
    EVT_START_IDLE,
    EVT_START_HOMING,
    EVT_START_INIT,
    EVT_START_BEND,
    EVT_START_COOL,
    EVT_START_COMPLETE,
    EVT_ERROR
} system_event_t;

/* ── Error codes ── */
typedef enum {
    ERROR_NONE,
    ERROR_HOMING_FAILED,
    ERROR_HEATER_TEMP,
    ERR_STEPPER,
} error_code_t;

/* ── State machine context ── */
typedef struct {
    system_state_t current;
    system_state_t previous;
    error_code_t   error_code;

    bool temp_reached;
    bool bend_in_place;
    bool bend_complete;
    bool cool_complete;
} sm_ctx_t;

/* ── Display message ── */
typedef struct {
    system_state_t state;
} display_msg_t;

/* ── Message queues ── */
extern struct k_msgq event_queue;
extern struct k_msgq display_queue;

/* -- User inputs -- */
typedef struct {
    int bend_angle;    /* degrees */
    int thickness;     /* mm */
    int bend_radii;    /* mm */
} user_inputs_t;

extern user_inputs_t g_inputs;
/* ── Global state machine instance ── */
extern sm_ctx_t g_sm;

/* ── State machine API ── */
void sm_init(void);
void sm_transition(system_state_t new_state);
const char *get_state_name(system_state_t state);

/* ── State entry functions ── */
void idle_entry(void);
void homing_entry(void);
void initialization_entry(void);
void bend_entry(void);
void cool_entry(void);
void complete_entry(void);
void error_entry(void);

static inline void event_post(system_event_t evt)
{
    k_msgq_put(&event_queue, &evt, K_NO_WAIT);
}

#endif /* STATE_H */