#ifndef POWER_MANAGEMENT_TASK_H_
#define POWER_MANAGEMENT_TASK_H_

// Shared with autotune_task.c so the tuner's safety margin always stays
// anchored to the same hard cutoff the reactive overheat-protection uses.
#define PM_THROTTLE_TEMP 75.0
#define PM_TPS546_THROTTLE_TEMP 105.0

typedef struct
{
    float fan_perc;
    uint16_t fan_rpm;
    uint16_t fan2_rpm;
    float chip_temp_avg;
    float chip_temp2_avg;
    float vr_temp;
    float voltage;
    float frequency_value;
    float actual_frequency;    
    float expected_hashrate;
    float power;
    float current;
    float core_voltage;
} PowerManagementModule;

void POWER_MANAGEMENT_init_frequency(void * pvParameters);

void POWER_MANAGEMENT_task(void * pvParameters);

#endif
