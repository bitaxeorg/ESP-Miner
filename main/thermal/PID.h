#ifndef PID_H
#define PID_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    // Tuning parameters
    float kp;           // Proportional gain
    float ki;           // Integral gain
    float kd;           // Derivative gain

    // Control parameters
    float setpoint;     // Target value
    float output_min;   // Minimum output limit
    float output_max;   // Maximum output limit

    // Internal state
    float last_error;   // Previous error for derivative calculation
    float integral;     // Accumulated error
    float last_input;   // Last measured input for derivative calculation

    // Anti-windup and configuration
    float max_integral; // Maximum integral accumulation
    bool inverse;       // Inverse control mode
} PIDController;

// Initialize PID controller
void PID_init(
    PIDController* pid, 
    float kp, 
    float ki, 
    float kd, 
    float setpoint, 
    float output_min, 
    float output_max,
    bool inverse
);

// Compute PID output
float PID_compute(
    PIDController* pid, 
    float measured_value, 
    float dt
);

// Reset PID controller state
void PID_reset(PIDController* pid);

// Set new PID tunings
void PID_set_tunings(
    PIDController* pid, 
    float kp, 
    float ki, 
    float kd
);

// Set output limits
void PID_set_output_limits(
    PIDController* pid, 
    float min, 
    float max
);

#endif // PID_H