#ifndef THERMAL_H
#define THERMAL_H

#include <stdbool.h>
#include <esp_err.h>

#include "EMC2101.h"
#include "EMC2103.h"
#include "global_state.h"

 master
// Temperature trend enum
typedef enum {
    TEMP_TREND_STABLE,
    TEMP_TREND_RISING,
    TEMP_TREND_FALLING
} TempTrend;

esp_err_t Thermal_init(DeviceModel device_model, bool polarity);

esp_err_t Thermal_init(DeviceModel device_model);
 master
esp_err_t Thermal_set_fan_percent(DeviceModel device_model, float percent);
uint16_t Thermal_get_fan_speed(DeviceModel device_model);

float Thermal_get_chip_temp(GlobalState * GLOBAL_STATE);

// New thermal management functions
float Thermal_calculate_fan_speed(float current_temp, float target_temp);
float Thermal_predict_temperature(float current_temp);
esp_err_t Thermal_adaptive_fan_control(GlobalState * GLOBAL_STATE, float target_temp);
TempTrend Thermal_get_temp_trend(GlobalState * GLOBAL_STATE);

#endif // THERMAL_H