#ifndef THERMAL_H
#define THERMAL_H

#include <esp_err.h>


esp_err_t Thermal_init();
esp_err_t Thermal_set_fan_percent(float percent);
uint16_t Thermal_get_fan_speed();

esp_err_t Thermal_init();
float Thermal_get_chip_temp2();
float Thermal_get_chip_temp();
uint16_t Thermal_get_fan_speed();

#endif // THERMAL_H
