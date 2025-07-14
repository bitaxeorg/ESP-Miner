#pragma once

typedef struct
{
    uint16_t fan_perc;
    uint16_t fan_rpm;
    float chip_temp[6];
    float chip_temp_avg;
    float vr_temp;
    float voltage;
    float frequency_value;
    float power;
    float current;
    float core_voltage;
} PowerManagementModule;

extern PowerManagementModule POWER_MANAGEMENT_MODULE;