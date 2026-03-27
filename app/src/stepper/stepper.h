#pragma once

/* Motor config — shared across any file that includes this header */
#define MICROSTEPS      100
#define STEPS_PER_REV   20000UL
#define TARGET_RPM      30.0f
#define DIR_FORWARD     1
#define DIR_BACKWARD    0

/* Public functions */
int   stepper_init(void);
void  stepper_move_to_degrees(float deg);
long  stepper_get_steps(void);
float stepper_get_degrees(void);