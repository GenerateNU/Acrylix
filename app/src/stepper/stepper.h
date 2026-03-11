#pragma once

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/* Motor config — shared across any file that includes this header */
#define MICROSTEPS      100
#define STEPS_PER_REV   20000
#define TARGET_RPM      100.0f
#define DIR_FORWARD     1
#define DIR_BACKWARD    0

/* Public functions — anything main.c (or other files) need to call */
int  stepper_init(void);
void rotate_steps(long steps, int dir);