#ifndef THERMAL_H
#define THERMAL_H

#include <stdbool.h>
#include <esp_err.h>

#include "EMC2101.h"
#include "EMC2103.h"

typedef struct {
    float temp1;
    float temp2;
} thermal_temps_t;

esp_err_t Thermal_init();
esp_err_t Thermal_set_fan_percent(float percent);
uint16_t Thermal_get_fan_speed();

esp_err_t Thermal_init();
thermal_temps_t Thermal_get_chip_temps();
void Thermal_get_temperatures(float * temp1, float * temp2);
bool Thermal_has_dual_sensors();
float Thermal_get_chip_temp();
uint16_t Thermal_get_fan_speed();

#endif // THERMAL_H