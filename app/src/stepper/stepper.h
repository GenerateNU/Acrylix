#pragma once

#include <zephyr/kernel.h>

/* Public API */
int   stepper_init(void);
void  stepper_move_degrees(float degrees, float rpm);
long  stepper_get_steps(void);
float stepper_get_degrees(void);
void stepper_emergency_stop(void);
void stepper_reset_position(void);
float stepper_get_progress(void);
