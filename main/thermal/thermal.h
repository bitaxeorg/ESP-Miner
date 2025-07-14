#ifndef THERMAL_H
#define THERMAL_H

#include <stdbool.h>
#include <esp_err.h>

#include "EMC2101.h"
#include "EMC2103.h"


esp_err_t Thermal_init();
esp_err_t Thermal_set_fan_percent(float percent);
uint16_t Thermal_get_fan_speed();

float Thermal_get_chip_temp();

#endif // THERMAL_H