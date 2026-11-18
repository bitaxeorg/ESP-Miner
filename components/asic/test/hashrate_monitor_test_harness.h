#ifndef HASHRATE_MONITOR_TEST_HARNESS_H_
#define HASHRATE_MONITOR_TEST_HARNESS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../../../main/device_config.h"
#include "../../../main/tasks/hashrate_monitor_task.h"

typedef struct {
    float current_hashrate;
    float error_percentage;
    float hashrate_1m;
    float hashrate_10m;
    float hashrate_1h;
    bool mining_paused;
} HashrateMonitorTestSystemModule;

typedef struct HashrateMonitorTestState {
    struct {
        struct {
            uint16_t asic_count;
            struct {
                Asic id;
                uint8_t hash_domains;
            } asic;
        } family;
    } DEVICE_CONFIG;
    HashrateMonitorModule HASHRATE_MONITOR_MODULE;
    HashrateMonitorTestSystemModule SYSTEM_MODULE;
    struct {
        bool is_active;
    } SELF_TEST_MODULE;
    bool ASIC_initalized;
} HashrateMonitorTestState;

void hashrate_monitor_test_task(void *state);
void hashrate_monitor_test_register_read(void *state, register_type_t type,
                                       uint8_t asic, uint32_t value,
                                       uint64_t timestamp_us);
void hashrate_monitor_test_reset_measurements(void *state);
bool hashrate_monitor_test_has_partial_stall(void *state, int *stalled_count);

int64_t hashrate_monitor_test_time_us(void);
TickType_t hashrate_monitor_test_ticks(void);
void hashrate_monitor_test_delay(TickType_t ticks);
void hashrate_monitor_test_delay_until(TickType_t *last_wake, TickType_t ticks);
void hashrate_monitor_test_read_registers(HashrateMonitorTestState *state);
void hashrate_monitor_test_noinit_update(HashrateMonitorTestSystemModule *system);
void *hashrate_monitor_test_malloc(size_t size, uint32_t caps);

#endif
