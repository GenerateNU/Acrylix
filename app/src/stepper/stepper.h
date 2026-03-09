#pragma once

#define STEPS_PER_REV   20000
#define DIR_FORWARD     1
#define DIR_BACKWARD    0

int  stepper_init(void);
void rotate_steps(long steps, int dir);