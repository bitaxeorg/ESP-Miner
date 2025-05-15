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

// New power optimization functions
float Power_calculate_min_voltage(GlobalState * GLOBAL_STATE, float frequency);
esp_err_t Power_optimize_voltage(GlobalState * GLOBAL_STATE);
float Power_calculate_efficiency(GlobalState * GLOBAL_STATE);
esp_err_t Power_apply_profile(GlobalState * GLOBAL_STATE, PowerProfile profile);

#endif // POWER_H
