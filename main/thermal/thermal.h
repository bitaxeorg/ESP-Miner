#ifndef THERMAL_H
#define THERMAL_H

#include <esp_err.h>

#include "global_state.h"

esp_err_t Thermal_init(DeviceConfig * DEVICE_CONFIG);
esp_err_t Thermal_set_fan_percent(DeviceConfig * DEVICE_CONFIG, float percent);
uint16_t Thermal_get_fan_speed(DeviceConfig * DEVICE_CONFIG);
uint16_t Thermal_get_fan2_speed(DeviceConfig * DEVICE_CONFIG);

float Thermal_get_chip_temp(GlobalState * GLOBAL_STATE);
float Thermal_get_chip_temp2(GlobalState * GLOBAL_STATE);

#endif // THERMAL_H
