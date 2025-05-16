#ifndef POWER_H
#define POWER_H

#include <esp_err.h>
#include "global_state.h"

float Power_get_current(GlobalState * GLOBAL_STATE);
float Power_get_power(GlobalState * GLOBAL_STATE);
float Power_get_input_voltage(GlobalState * GLOBAL_STATE);
float Power_get_vreg_temp(GlobalState * GLOBAL_STATE);

#endif // POWER_H
