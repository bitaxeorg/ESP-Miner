#ifndef HASHRATE_MONITOR_TASK_H_
#define HASHRATE_MONITOR_TASK_H_

#include "asic_common.h"

typedef struct {
    uint32_t value;
    uint64_t time_us;
    float hashrate;
} measurement_t;

typedef struct {
    measurement_t* total_measurement;
    measurement_t** domain_measurements;
    measurement_t* error_measurement;

    bool is_initialized;
} HashrateMonitorModule;

void hashrate_monitor_task(void *pvParameters);
void hashrate_monitor_register_read(void *pvParameters, register_type_t register_type, uint8_t asic_nr, uint32_t value);
void hashrate_monitor_reset_measurements(void *pvParameters);

void update_hashrate(measurement_t * measurement, uint32_t value);
void update_hash_counter(measurement_t * measurement, uint32_t value, uint64_t time_us);

/**
 * Check for sustained hashrate anomalies and auto-recover ASICs if needed.
 *
 * When the hashrate drops below a dynamic lower threshold or exceeds an upper
 * threshold for multiple consecutive polls, this function reinitializes the
 * ASICs using live recovery mode to restore normal operation without a full
 * system reboot.
 *
 * Inspired by TinyChipHub ESP-Miner-TCH stability improvements.
 */
void check_hashrate_anomaly(void *pvParameters, float current_hashrate);

#endif /* HASHRATE_MONITOR_TASK_H_ */
