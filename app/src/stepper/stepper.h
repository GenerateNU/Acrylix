#pragma once

/* Motor config — shared across any file that includes this header */
#define MICROSTEPS      100
#define STEPS_PER_REV   20000
#define TARGET_RPM      100.0f
#define DIR_FORWARD     1
#define DIR_BACKWARD    0

/* Public functions — anything main.c (or other files) need to call */
int  stepper_init(void);
void moveToDegree(float deg);
void moveToSteps(long targetSteps);
void stepper_test(void);