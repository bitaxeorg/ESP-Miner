#ifndef PID_H
#define PID_H

#include <stdbool.h>

#define AUTOMATIC 1
#define MANUAL 0
typedef enum {
    PID_DIRECT = 0,
    PID_REVERSE = 1
} PIDDirection;

typedef enum {
    PID_P_ON_M = 0, 
    PID_P_ON_E = 1
} PIDProportionalMode;

typedef struct {
    float dispKp;
    float dispKi;
    float dispKd;

    float kp;
    float ki;
    float kd;

    PIDDirection controllerDirection;
    PIDProportionalMode pOn;
    bool pOnE;

    float *input;
    float *output;
    float *setpoint;

    unsigned long lastTime;
    unsigned long sampleTime;
    float outMin;
    float outMax;
    bool inAuto;

    float outputSum;
    float lastInput;
} PIDController;

void pid_init(PIDController *pid, float *input, float *output, float *setpoint,
              float Kp, float Ki, float Kd, PIDProportionalMode POn, PIDDirection ControllerDirection);

void pid_set_mode(PIDController *pid, int mode);
bool pid_compute(PIDController *pid);
void pid_set_output_limits(PIDController *pid, float min, float max);
void pid_set_tunings(PIDController *pid, float Kp, float Ki, float Kd);
void pid_set_tunings_adv(PIDController *pid, float Kp, float Ki, float Kd, PIDProportionalMode POn);
void pid_set_sample_time(PIDController *pid, int newSampleTime);
void pid_set_controller_direction(PIDController *pid, PIDDirection direction);
void pid_initialize(PIDController *pid);

// Getter functions
float pid_get_kp(PIDController *pid);
float pid_get_ki(PIDController *pid);
float pid_get_kd(PIDController *pid);
float pid_get_ti(PIDController *pid);
float pid_get_td(PIDController *pid);
int pid_get_mode(PIDController *pid);
PIDDirection pid_get_direction(PIDController *pid);

#endif
