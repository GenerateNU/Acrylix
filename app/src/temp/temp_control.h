#ifndef TEMP_CONTROL_H
#define TEMP_CONTROL_H

/* Initializes the ADC sensor thread and SSR, creates the heater thread
 * in a suspended state. Call heater_start() to begin PI control. */
void temp_init(void);

/* Resume the PI control thread — SSR will begin switching toward setpoint. */
void heater_start(void);

/* Suspend the PI control thread and force SSR off immediately. */
void heater_stop(void);

#endif
