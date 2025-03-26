#ifndef POWER_H
#define POWER_H

#include <esp_err.h>
#include "global_state.h"


esp_err_t Power_disable(GlobalState * GLOBAL_STATE);

float Power_get_current(GlobalState * GLOBAL_STATE);
float Power_get_power(GlobalState * GLOBAL_STATE);
float Power_get_input_voltage(GlobalState * GLOBAL_STATE);
float Power_get_vreg_temp(GlobalState * GLOBAL_STATE);
float Power_get_max_settings(GlobalState * GLOBAL_STATE);
int Power_get_nominal_voltage(GlobalState * GLOBAL_STATE);

#endif // POWER_H
