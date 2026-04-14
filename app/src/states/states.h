/* states.h */
#ifndef STATES_H
#define STATES_H

#include <zephyr/kernel.h>
#include <stdbool.h>
#include "../app_events.h"   /* system_event_t, user_inputs_t, display_msg_t,
                             event_queue, display_queue, g_inputs, event_post */


/* ══════════════════════════════════════════════════════════════
 *  Error codes
 * ══════════════════════════════════════════════════════════════ */

typedef enum {
    ERROR_NONE = 0,
    ERROR_HOMING_FAILED,
    ERROR_HEATER_TEMP,
    ERR_STEPPER,
} error_code_t;

/* ══════════════════════════════════════════════════════════════
 *  State machine context
 * ══════════════════════════════════════════════════════════════ */

typedef struct {
    system_state_t current;
    system_state_t previous;
    error_code_t   error_code;
    bool           temp_reached;
    bool           bend_in_place;
    bool           bend_complete;
    bool           cool_complete;
} sm_ctx_t;

/* ── Global state machine instance — defined in states.c ── */
extern sm_ctx_t g_sm;

/* ══════════════════════════════════════════════════════════════
 *  State machine API
 * ══════════════════════════════════════════════════════════════ */

void        sm_init(void);
void        sm_transition(system_state_t new_state);
const char *get_state_name(system_state_t state);

/* ══════════════════════════════════════════════════════════════
 *  State entry functions
 * ══════════════════════════════════════════════════════════════ */

void idle_entry(void);
void homing_entry(void);
void initialization_entry(void);
void bend_entry(void);
void cool_entry(void);
void complete_entry(void);
void error_entry(void);

#endif /* STATES_H */