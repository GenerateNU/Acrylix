#ifndef DISPLAY_H
#define DISPLAY_H

#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>

#ifdef CONFIG_LVGL
#include <lvgl.h>
#endif

/* Initialize the display and LVGL */
int display_init(void);

/* Call this in your main loop every 10ms */
void display_update(void);

/* UI creation functions - add more as your project grows */
void display_create_home_screen(void);
void display_set_state(int state);
//void display_update_value(const char *value);
//void display_update_progress(int pct, int min, int sec);
void input_selection_next(void);
void input_selection_prev(void);
void input_selection_update_value(int delta);

/*encoder */
int encoder_get_position(void);
int encoder_get_direction(void);

/* animation */
void input_selection_next(void);
void input_selection_prev(void);
void input_selection_update_value(int delta);

#endif /* DISPLAY_H */