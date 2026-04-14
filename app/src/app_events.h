/* app_events.h
 * Shared event types and user inputs accessed by both display.c and states.c.
 * Neither display nor states should include each other directly.
 */
#ifndef APP_EVENTS_H
#define APP_EVENTS_H

#include <zephyr/kernel.h>

/* ── Events — posted by display, consumed by state thread ── */
typedef enum {
    STATE_IDLE = 0,
    STATE_HOMING,
    STATE_INITIALIZATION,
    STATE_BEND,
    STATE_COOL,
    STATE_COMPLETE,
    STATE_ERROR
} system_state_t;

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

typedef struct {
    int  pct;
    int  min;
    int  sec;
    bool pending;
    float bend_fraction;
    bool  bend_pending;  
} progress_update_t;

extern progress_update_t g_progress;

/* ── User inputs — written by display, read by state thread ── */
typedef struct {
    int bend_angle;   /* degrees, 0-90 */
    int thickness;    /* 0 = 1/16 in, 1 = 1/8 in */
    int bend_radii;   /* reserved */
} user_inputs_t;

/* ── Display message — sent over display_queue on state transition ── */
typedef struct {
    int state;   /* maps to display_set_state() parameter */
} display_msg_t;

/* ── Message queues — defined in main.c ── */
extern struct k_msgq event_queue;
extern struct k_msgq display_queue;

/* ── Global user inputs — defined in states.c ── */
extern user_inputs_t g_inputs;

/* ── Inline event helper ── */
static inline void event_post(system_event_t evt)
{
    k_msgq_put(&event_queue, &evt, K_NO_WAIT);
}

#endif /* APP_EVENTS_H */