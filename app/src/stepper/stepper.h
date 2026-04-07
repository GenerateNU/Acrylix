#pragma once

#include <zephyr/kernel.h>

/* Public API — matches stepper.c implementation */
int   stepper_init(void);
void  stepper_move_to_degrees(float deg);
long  stepper_get_steps(void);
float stepper_get_degrees(void);
