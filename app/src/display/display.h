/* display.h */
#ifndef DISPLAY_H
#define DISPLAY_H

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Semaphore given by process_screen() once all LVGL objects are built.
 *        run_countdown() must take this before writing to g_progress so it
 *        never fires display_update_progress() against a partially-constructed
 *        screen.
 */
extern struct k_sem display_screen_ready;

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
 *   2 = STATE_BEND        → bending screen
 *   3 = STATE_COOL        → cooling screen
 *   4 = STATE_COMPLETE    → complete screen
 *   5 = STATE_ERROR       → error screen
 *   6 = STATE_BEND        → bending screen
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
 * @brief Post a bend progress update from the state thread.
 *        Thread-safe: writes to a mailbox consumed by display_update().
 *        Use this instead of calling LVGL directly from the state thread.
 * @param fraction  0.0 (start) to 1.0 (complete)
 */
void display_post_bend_progress(float fraction);

/**
 * @brief Post a temperature update from the state thread during warmup.
 *        Thread-safe: writes to a mailbox consumed by display_update().
 * @param temp_c  Current temperature in degrees C.
 */
void display_post_heat_temp(float temp_c);

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