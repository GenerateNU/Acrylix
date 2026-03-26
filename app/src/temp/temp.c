#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <zephyr/input/input.h> 
#include <lvgl.h>

#define KP 0.5              // proportional gain
#define KI 0.1              // integral gain
#define TS 0.01             // sample time (0.01 seconds)
#define MAX_OUTPUT 255
#define MIN_OUTPUT 0
#define INTEGRAL_MAX 500    // based on max?
#define INTEGRAL_MIN -500

double integral_term = 0.0;
double setpoint = 150.0;            // target value
double process_variable = 0;      // sensor reading

double update_pi_controller(double input_value) {
    // Calculate error
    double error = setpoint - input_value;

    // Update integral term
    integral_term += error * TS;

    // Clamp integral
    if (integral_term > INTEGRAL_MAX) integral_term = INTEGRAL_MAX;
    else if (integral_term < INTEGRAL_MIN) integral_term = INTEGRAL_MIN;

    // Calculate output
    double output = (KP * error) + (KI * integral_term);

    // Clamp output to maintain minimum or maximum value
    if (output > MAX_OUTPUT) output = MAX_OUTPUT;
    else if (output < MIN_OUTPUT) output = MIN_OUTPUT;

    return output;
}

int test_main() {
    // Example usage in a main loop 
    
    for (int cycle = 0; cycle < 1000; cycle++) {
        // In a real system, 'process_variable' would come from a sensor (e.g., ADC)
        // We simulate a simple response here for demonstration
        double control = update_pi_controller(process_variable);
        
        process_variable += (control - process_variable) * 0.05;
        printk("Cycle %d, PV: %.2f, Output: %.2f\n", cycle, process_variable, control);

        // The update_pi_controller function is the 'PI loop' logic called on each cycle
        // The output of this function would control an actuator (e.g., PWM signal)

        // Add print statements for debugging/monitoring
        // printf("Cycle %d, PV: %.2f, Output: %.2f\n", cycle, process_variable, update_pi_controller(process_variable));
    }

    return 0;
}

// Integral term eliminates error by summing the error over time