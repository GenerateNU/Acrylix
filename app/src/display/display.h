/* display.h */
#ifndef DISPLAY_H
#define DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

/* ══════════════════════════════════════════════════════════════
 *  Init / update — call from display thread
 * ══════════════════════════════════════════════════════════════ */

/**
 * @brief Initialize display hardware, encoder, buttons, and LVGL.
 *        Shows the directions screen on success.
 * @return 0 on success, -1 if display device is not ready.
 */
int display_init(void);

/**
 * @brief Call every ~10 ms from the display thread main loop.
 *        Handles button flags, thickness toggle, and LVGL task handler.
 */
void display_update(void);

/* ══════════════════════════════════════════════════════════════
 *  Screen API — driven by state machine via display_queue
 * ══════════════════════════════════════════════════════════════ */

/**
 * @brief Show the directions screen (STATE_IDLE).
 *        Also called internally by display_init().
 */
void display_create_home_screen(void);

/**
 * @brief Switch to the screen corresponding to a state machine state.
 *
 * @param state  Mapping:
 *   0 = STATE_IDLE        → directions screen
 *   1 = STATE_INIT        → input selection (bend radius first)
 *   2 = STATE_BEND ph1    → heating screen
 *   3 = STATE_COOL        → cooling screen
 *   4 = STATE_COMPLETE    → complete screen
 *   5 = STATE_ERROR       → error screen
 *   6 = STATE_BEND ph2    → bending screen
 */
void display_set_state(int state);

/* ══════════════════════════════════════════════════════════════
 *  Live data update API — called from state thread during
 *  heating, bending, and cooling countdowns
 * ══════════════════════════════════════════════════════════════ */

/**
 * @brief Update the value label on the process screen.
 *        Example: current temperature as a string.
 * @param value  Null-terminated string to display.
 */
void display_update_value(const char *value);

/**
 * @brief Update the progress bar and time remaining on the process screen.
 * @param pct  Percentage complete (0–100).
 * @param min  Minutes remaining.
 * @param sec  Seconds remaining (0–59).
 */
void display_update_progress(int pct, int min, int sec);

/* Update the progress bar for the bend angle */
void display_update_bend_progress(float fraction);   /* 0.0 to 1.0 */

/* ══════════════════════════════════════════════════════════════
 *  Encoder public API
 * ══════════════════════════════════════════════════════════════ */

/**
 * @brief Get the current encoder count (accumulated since init).
 */
int encoder_get_position(void);

/**
 * @brief Get the last encoder direction (+1 CW, -1 CCW, 0 no movement).
 *        Stub — returns 0 until wired to state machine.
 */
int encoder_get_direction(void);

/* ══════════════════════════════════════════════════════════════
 *  Hardware init helpers — called internally by display_init()
 *  Exposed here so they can be called independently if needed.
 * ══════════════════════════════════════════════════════════════ */

void encoder_init(void);
void buttons_init(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_H */