#ifndef TEST_STUB_GLOBAL_STATE_H
#define TEST_STUB_GLOBAL_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "scoreboard.h"
#include "asic_job_slot.h"

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
    asic_job_store_t job_store;
    struct {
        float frequency_value;
        float actual_frequency;
    } POWER_MANAGEMENT_MODULE;
    bool ASIC_initalized;
    uint32_t ASIC_work_epoch;
    struct {
        float process_time;
        Scoreboard scoreboard;
    } SYSTEM_MODULE;
    struct {
        bool is_active;
    } SELF_TEST_MODULE;
} GlobalState;

#endif /* TEST_STUB_GLOBAL_STATE_H */
