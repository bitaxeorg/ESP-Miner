#ifndef POWER_MANAGEMENT_TASK_H_
#define POWER_MANAGEMENT_TASK_H_

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
    float current_frequency;   // Track the actual current frequency that may differ due to throttling
    float target_voltage;      // Desired voltage setting
    float power;
    float current;
    float power_efficiency;    // Power efficiency metric (hashrate/watt)
    float voltage_offset;      // Voltage offset for fine-tuning
    bool dynamic_voltage;      // Whether to use dynamic voltage scaling
    uint32_t throttle_count;   // Counter for throttling events
    uint32_t last_optimization_time; // Timestamp of last optimization
} PowerManagementModule;

void POWER_MANAGEMENT_task(void * pvParameters);

#endif
