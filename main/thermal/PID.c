#include "PID.h"
#include "esp_timer.h" // For esp_timer_get_time()

static unsigned long millis() {
    return (unsigned long)(esp_timer_get_time() / 1000);
}

/**
 * @param DIRECT
 * An increase in input (e.g., temperature) causes an increase in output (e.g., fan speed).
 * Use this when the output should move in the same direction as the error.
 * 
 * @param REVERSE
 * An increase in input causes a decrease in output.
 * Use this when the output should move in the opposite direction of the error.
 */
void pid_init(PIDController *pid, double *input, double *output, double *setpoint,
              double Kp, double Ki, double Kd, PIDProportionalMode POn, PIDDirection ControllerDirection) {
    pid->input = input;
    pid->output = output;
    pid->setpoint = setpoint;
    pid->inAuto = false;

    pid_set_output_limits(pid, 0, 255);
    pid->sampleTime = 100;

    pid_set_controller_direction(pid, ControllerDirection);
    pid_set_tunings_adv(pid, Kp, Ki, Kd, POn);

    pid->lastTime = millis() - pid->sampleTime;
}
/**
 * @param AUTOMATIC mode:
 * The PID controller continuously calculates the output based on the current input, setpoint, and tuning parameters. It actively adjusts the output to minimize the error.
 * 
 * @param MANUAL mode:
 * The PID controller stops automatic adjustments. The output is either fixed or controlled externally. This is useful for tuning, testing, or when you want to override the PID temporarily.
 */

void pid_set_mode(PIDController *pid, int mode) {
    bool newAuto = (mode == AUTOMATIC);
    if (newAuto && !pid->inAuto) {
        pid_initialize(pid);
    }
    pid->inAuto = newAuto;
}

bool pid_compute(PIDController *pid) {
    if (!pid->inAuto) return false;

    unsigned long now = millis();
    unsigned long timeChange = now - pid->lastTime;

    if (timeChange >= pid->sampleTime) {
        double input = *(pid->input);
        double error = *(pid->setpoint) - input;
        double dInput = input - pid->lastInput;
        pid->outputSum += pid->ki * error;

        if (!pid->pOnE) pid->outputSum -= pid->kp * dInput;

        if (pid->outputSum > pid->outMax) pid->outputSum = pid->outMax;
        else if (pid->outputSum < pid->outMin) pid->outputSum = pid->outMin;

        double output = pid->pOnE ? pid->kp * error : 0;
        output += pid->outputSum - pid->kd * dInput;

        if (output > pid->outMax) {
            pid->outputSum -= output - pid->outMax;
            output = pid->outMax;
        } else if (output < pid->outMin) {
            pid->outputSum += pid->outMin - output;
            output = pid->outMin;
        }

        *(pid->output) = output;
        pid->lastInput = input;
        pid->lastTime = now;
        return true;
    }
    return false;
}

void pid_set_tunings_adv(PIDController *pid, double Kp, double Ki, double Kd, PIDProportionalMode POn) {
    if (Kp < 0 || Ki < 0 || Kd < 0) return;

    pid->pOn = POn;
    pid->pOnE = (POn == PID_P_ON_E);

    pid->dispKp = Kp;
    pid->dispKi = Ki;
    pid->dispKd = Kd;

    double sampleTimeInSec = ((double)pid->sampleTime) / 1000.0;
    pid->kp = Kp;
    pid->ki = Ki * sampleTimeInSec;
    pid->kd = Kd / sampleTimeInSec;

    if (pid->controllerDirection == PID_REVERSE) {
        pid->kp = -pid->kp;
        pid->ki = -pid->ki;
        pid->kd = -pid->kd;
    }
}

void pid_set_tunings(PIDController *pid, double Kp, double Ki, double Kd) {
    pid_set_tunings_adv(pid, Kp, Ki, Kd, pid->pOn);
}

void pid_set_sample_time(PIDController *pid, int newSampleTime) {
    if (newSampleTime > 0) {
        double ratio = (double)newSampleTime / (double)pid->sampleTime;
        pid->ki *= ratio;
        pid->kd /= ratio;
        pid->sampleTime = newSampleTime;
    }
}

void pid_set_output_limits(PIDController *pid, double min, double max) {
    if (min >= max) return;
    pid->outMin = min;
    pid->outMax = max;

    if (pid->inAuto) {
        if (*(pid->output) > max) *(pid->output) = max;
        else if (*(pid->output) < min) *(pid->output) = min;

        if (pid->outputSum > max) pid->outputSum = max;
        else if (pid->outputSum < min) pid->outputSum = min;
    }
}

void pid_set_controller_direction(PIDController *pid, PIDDirection direction) {
    if (pid->inAuto && direction != pid->controllerDirection) {
        pid->kp = -pid->kp;
        pid->ki = -pid->ki;
        pid->kd = -pid->kd;
    }
    pid->controllerDirection = direction;
}

void pid_initialize(PIDController *pid) {
    pid->outputSum = *(pid->output);
    pid->lastInput = *(pid->input);
    if (pid->outputSum > pid->outMax) pid->outputSum = pid->outMax;
    else if (pid->outputSum < pid->outMin) pid->outputSum = pid->outMin;
}

double pid_get_kp(PIDController *pid) { return pid->dispKp; }
double pid_get_ki(PIDController *pid) { return pid->dispKi; }
double pid_get_kd(PIDController *pid) { return pid->dispKd; }
double pid_get_ti(PIDController *pid) { return pid->dispKp / pid->dispKi; }
double pid_get_td(PIDController *pid) { return pid->dispKd / pid->dispKp; }
int pid_get_mode(PIDController *pid) { return pid->inAuto ? AUTOMATIC : MANUAL; }
PIDDirection pid_get_direction(PIDController *pid) { return pid->controllerDirection; }
