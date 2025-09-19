#ifndef POWER_H
#define POWER_H

#include <esp_err.h>
#include "global_state.h"

float Power_get_current(DeviceConfig * DEVICE_CONFIG);
float Power_get_power(DeviceConfig * DEVICE_CONFIG);
float Power_get_input_voltage(DeviceConfig * DEVICE_CONFIG);
float Power_get_vreg_temp(DeviceConfig * DEVICE_CONFIG);

#endif // POWER_H
