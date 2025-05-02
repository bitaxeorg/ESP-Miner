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
    double dispKp;
    double dispKi;
    double dispKd;

    double kp;
    double ki;
    double kd;

    PIDDirection controllerDirection;
    PIDProportionalMode pOn;
    bool pOnE;

    double *input;
    double *output;
    double *setpoint;

    unsigned long lastTime;
    unsigned long sampleTime;
    double outMin;
    double outMax;
    bool inAuto;

    double outputSum;
    double lastInput;
} PIDController;

void pid_init(PIDController *pid, double *input, double *output, double *setpoint,
              double Kp, double Ki, double Kd, PIDProportionalMode POn, PIDDirection ControllerDirection);

void pid_set_mode(PIDController *pid, int mode);
bool pid_compute(PIDController *pid);
void pid_set_output_limits(PIDController *pid, double min, double max);
void pid_set_tunings(PIDController *pid, double Kp, double Ki, double Kd);
void pid_set_tunings_adv(PIDController *pid, double Kp, double Ki, double Kd, PIDProportionalMode POn);
void pid_set_sample_time(PIDController *pid, int newSampleTime);
void pid_set_controller_direction(PIDController *pid, PIDDirection direction);
void pid_initialize(PIDController *pid);

// Getter functions
double pid_get_kp(PIDController *pid);
double pid_get_ki(PIDController *pid);
double pid_get_kd(PIDController *pid);
double pid_get_ti(PIDController *pid);
double pid_get_td(PIDController *pid);
int pid_get_mode(PIDController *pid);
PIDDirection pid_get_direction(PIDController *pid);

#endif
