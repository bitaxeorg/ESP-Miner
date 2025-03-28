#include "PID.h"
#include <math.h>

void PID_init(
    PIDController* pid, 
    float kp, 
    float ki, 
    float kd, 
    float setpoint, 
    float output_min, 
    float output_max,
    bool inverse
) {
    // Initialize PID parameters
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->setpoint = setpoint;
    pid->output_min = output_min;
    pid->output_max = output_max;
    pid->inverse = inverse;

    // Reset internal state
    pid->last_error = 0;
    pid->integral = 0;
    pid->last_input = 0;

    // Set a reasonable max integral to prevent windup
    pid->max_integral = (output_max - output_min) / 2.0;
}

float PID_compute(
    PIDController* pid, 
    float measured_value, 
    float dt
) {
    float error;
    
    // For temperature-to-fan control
    if (pid->inverse) {
        // For inverse control (fan control):
        // - Positive error when temperature is ABOVE setpoint (need more cooling)
        // - Negative error when temperature is BELOW setpoint (need less cooling)
        error = measured_value - pid->setpoint;
    } else {
        // Standard control:
        // - Positive error when measured value is BELOW setpoint
        // - Negative error when measured value is ABOVE setpoint
        error = pid->setpoint - measured_value;
    }

    // For fan control, if temperature is below setpoint, just return minimum fan speed
    if (pid->inverse && error <= 0) {
        pid->last_error = error;
        pid->last_input = measured_value;
        pid->integral = 0; // Reset integral when below setpoint
        return pid->output_min;
    }

    // Compute proportional term
    float p_term = pid->kp * error;

    // Compute integral term with anti-windup
    pid->integral += error * dt;
    
    // Limit integral to prevent windup
    pid->integral = fmaxf(0, fminf(pid->integral, pid->max_integral));
    
    float i_term = pid->ki * pid->integral;

    // Compute derivative term
    float derivative = (measured_value - pid->last_input) / dt;
    
    // For fan control, we want to increase fan when temperature is rising
    float d_term = pid->inverse ? pid->kd * derivative : -pid->kd * derivative;

    // Compute total output
    float output;
    
    if (pid->inverse) {
        // For fan control, directly map the PID terms to fan speed
        // This ensures the fan responds immediately to temperature changes
        output = pid->output_min + p_term + i_term + d_term;
        
        // Ensure a minimum response for small errors
        if (error > 0) {
            float min_response = pid->output_min + (error * 5.0f);
            output = fmaxf(output, min_response);
        }
    } else {
        // Standard PID control for non-inverse applications
        output = p_term + i_term + d_term;
    }

    // Map the output to the range [output_min, output_max]
    output = fmaxf(pid->output_min, fminf(output, pid->output_max));

    // Update state for next iteration
    pid->last_error = error;
    pid->last_input = measured_value;

    return output;
}

void PID_reset(PIDController* pid) {
    pid->last_error = 0;
    pid->integral = 0;
    pid->last_input = 0;
}

void PID_set_tunings(
    PIDController* pid, 
    float kp, 
    float ki, 
    float kd
) {
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
}

void PID_set_output_limits(
    PIDController* pid, 
    float min, 
    float max
) {
    pid->output_min = min;
    pid->output_max = max;
    pid->max_integral = (max - min) / 2.0;

    // Adjust current integral if it's outside new limits
    pid->integral = fmaxf(-pid->max_integral, 
                           fminf(pid->integral, pid->max_integral));
}
