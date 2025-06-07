#ifndef POWER_MANAGEMENT_TASK_H_
#define POWER_MANAGEMENT_TASK_H_

#include <stdint.h>
#include <stdbool.h>
#include "global_state.h"

typedef struct
{
    uint16_t fan_perc;
    uint16_t fan_rpm;
    float chip_temp[6];
    float chip_temp_avg;
    float vr_temp;
    float voltage;
    float frequency_multiplier;
    float frequency_value;
    float power;
    float current;
    bool HAS_POWER_EN;
    bool HAS_PLUG_SENSE;
} PowerManagementModule;

typedef struct
{
    uint8_t autotune_preset;      // Autotune preset
    int16_t targetPower;          // Target power in watts
    uint16_t targetDomainVoltage; // Target voltage in mV
    uint16_t targetFrequency;     // Target frequency in MHz
    uint8_t targetFanSpeed;       // Target fan speed in percentage
    uint8_t targetTemperature;    // Target temperature in °C
    float targetHashrate;         // Target hashrate in GH/s
    uint16_t maxPower;            // Maximum power in watts
    uint16_t maxDomainVoltage;    // Maximum domain voltage in mV
    uint16_t maxFrequency;        // Maximum frequency in MHz
    uint16_t minDomainVoltage;    // Minimum domain voltage in mV
    uint16_t minFrequency;        // Minimum frequency in MHz

} AutotuneModule;

// Preset data structure for device-specific safe values
typedef struct {
    const char* name;
    uint16_t domain_voltage_mv;  // Domain voltage in mV
    uint16_t frequency_mhz;      // Frequency in MHz
    uint8_t fan_speed_percent;   // Fan speed in percentage
} DevicePreset;


void POWER_MANAGEMENT_task(void * pvParameters);

// Simple preset function - applies preset immediately


#endif
