#ifndef POWER_H
#define POWER_H

#include <esp_err.h>
#include "global_state.h"

void Power_get_output(GlobalState * GLOBAL_STATE, float * power_out, float * current_out);
float Power_get_input_voltage(GlobalState * GLOBAL_STATE);
float Power_get_vreg_temp(GlobalState * GLOBAL_STATE);

#endif // POWER_H
