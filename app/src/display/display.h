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

/* UI creation functions */
void display_create_home_screen(void);
void direction_screen(void);
void bend_radius_screen(void);
void bend_angle_input_screen(void);
void thickness_screen(void);

/* Input selection flow (bend radius → bend angle → thickness) */
void input_selection_enter(void);
int  input_selection_next(void);   /* returns 1 when last step confirmed */
int  input_selection_prev(void);   /* returns -1 when back pressed on first step */

/* encoder */
int encoder_get_position(void);
int encoder_get_direction(void);

#endif /* DISPLAY_H */
