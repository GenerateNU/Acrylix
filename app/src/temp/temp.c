#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <zephyr/input/input.h> 
#include <lvgl.h>
#include <stdio.h>

// need to decide what 100% heater power switching speed will be

#define KP 0.5              // proportional gain (reaction to current error)
#define KI 0.1              // integral gain (elimination of leftover error that persists over time)
#define TS 0.01             // seconds per cycle
#define MAX_OUTPUT 100      // 100% heater power
#define MIN_OUTPUT 0        // no heater power
#define INTEGRAL_MAX 500    
#define INTEGRAL_MIN -500

double integral_term = 0.0;         // controller "memory" set to 0
double setpoint = 150.0;            // target temperature
double process_variable = 100.0;    // initial temperature

double update_pi_controller(double input_value) {

    double error = setpoint - input_value; // error is distance from target

    integral_term += error * TS; // add current error to total

    // cap integral to max or min values
    if (integral_term > INTEGRAL_MAX) integral_term = INTEGRAL_MAX;
    else if (integral_term < INTEGRAL_MIN) integral_term = INTEGRAL_MIN;

    double output = (KP * error) + (KI * integral_term); // PI formula
    // this is the value that the MCU will receive to turn high/low the pin to the SSR

    // cap heater output
    if (output > MAX_OUTPUT) output = MAX_OUTPUT;
    else if (output < MIN_OUTPUT) output = MIN_OUTPUT;

    return output;
}

int test_main() {
    printf("Cycle  |  Temp (PV)  |  Output\n");
    printf("-------|-------------|--------\n");

    for (int cycle = 0; cycle < 2000; cycle++) {
        double control = update_pi_controller(process_variable);

        // Simulated plant response: moves PV toward the control output
        //process_variable += (control - process_variable) * 0.05;
        // ^^ that line forces the process_variable to 83.33

        process_variable += (control / MAX_OUTPUT) * (setpoint - process_variable) * 0.05;
        // control / MAX_OUTPUT: normalize value between 0-1
        // setpoint - process_variable: difference between current and expected
        // 0.05: scaling response time of system

        // Print every 50 cycles to reduce noise
        if (cycle % 50 == 0) {
            printf("%6d | %11.4f | %7.4f\n", cycle, process_variable, control);
        }
    }

    printk("Completed.");

    return 0;
}