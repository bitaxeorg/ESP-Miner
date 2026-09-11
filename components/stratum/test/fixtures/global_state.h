#ifndef TEST_FIXTURE_GLOBAL_STATE_H
#define TEST_FIXTURE_GLOBAL_STATE_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*
 * Shared test view for the real job task and isolated BM13xx driver copies.
 * Only the fields read by those production files belong here.
 */
typedef struct GlobalState {
    void *create_jobs_task_handle;
    volatile uint8_t active_job_slot_idx;
    struct {
        struct {
            uint16_t asic_count;
            uint8_t voltage_domains;
            struct {
                bool hardware_version_rolling;
                uint8_t software_midstates;
                uint16_t difficulty;
                uint16_t core_count;
            } asic;
        } family;
    } DEVICE_CONFIG;
    struct {
        struct bm_job **active_jobs;
        uint8_t *valid_jobs;
        pthread_mutex_t valid_jobs_lock;
    } ASIC_TASK_MODULE;
    struct {
        float frequency_value;
        float actual_frequency;
    } POWER_MANAGEMENT_MODULE;
    bool ASIC_initalized;
} GlobalState;

#endif /* TEST_FIXTURE_GLOBAL_STATE_H */
