#ifndef POWER_H
#define POWER_H

#include <esp_err.h>
#include "global_state.h"

float Power_get_current();
float Power_get_power();
float Power_get_input_voltage();
float Power_get_vreg_temp();

#endif // POWER_H
