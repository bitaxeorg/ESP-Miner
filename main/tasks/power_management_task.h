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
    float frequency_value;
    float power;
    float current;
} PowerManagementModule;

void POWER_MANAGEMENT_task(void * pvParameters);

#endif
