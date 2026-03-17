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

/*encoder */
int encoder_get_position(void);
int encoder_get_direction(void);

#endif /* DISPLAY_H */