#ifndef POWER_MANAGEMENT_TASK_H_
#define POWER_MANAGEMENT_TASK_H_

typedef struct
{
    uint16_t fan_speed;
    float chip_temp;
    float voltage;
    float frequency_multiplier;
    float frequency_value;
    float power;
    float current;
    bool HAS_POWER_EN;
    bool HAS_PLUG_SENSE;
} PowerManagementModule;

static void automatic_fan_speed(float chip_temp);
void POWER_MANAGEMENT_task(void * pvParameters);

#endif