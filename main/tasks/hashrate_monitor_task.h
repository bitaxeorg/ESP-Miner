#ifndef HASHRATE_MONITOR_TASK_H_
#define HASHRATE_MONITOR_TASK_H_

#include "common.h"

typedef struct {
    uint32_t value;
    uint32_t time_ms;
    float hashrate;
} measurement_t;

typedef struct {
    measurement_t* total_measurement;
    measurement_t* domain_0_measurement;
    measurement_t* domain_1_measurement;
    measurement_t* domain_2_measurement;
    measurement_t* domain_3_measurement;
    measurement_t* error_measurement;

    float hashrate;
    int error_count;
    bool is_initialized;
} HashrateMonitorModule;

void hashrate_monitor_task(void *pvParameters);
void hashrate_monitor_register_read(void *pvParameters, register_type_t register_type, uint8_t asic_nr, uint32_t value);

#endif /* HASHRATE_MONITOR_TASK_H_ */
