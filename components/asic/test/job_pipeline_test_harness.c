#include "asic_internal.h"
#include "mining_test_bindings.h"
#include "job_pipeline_test_harness.h"

#include <setjmp.h>
#include <string.h>

#include "asic.h"
#include "esp_timer.h"
#include "global_state.h"
#include "system.h"

#include "../../../main/tasks/create_jobs_task.h"

static jmp_buf harness_exit;
static const job_pipeline_harness_event_t *harness_events;
static size_t harness_event_count;
static size_t harness_event_index;
static job_pipeline_harness_result_t *harness_result;
static int harness_job_frequency_ms;
static uint64_t harness_now_us;
static GlobalState harness_state;

static int64_t fake_esp_timer_get_time(void)
{
    return (int64_t)harness_now_us;
}

static BaseType_t fake_task_notify_wait(
    uint32_t bits_to_clear_on_entry, unsigned long bits_to_clear_on_exit,
    uint32_t *notification_value, TickType_t ticks_to_wait)
{
    (void)bits_to_clear_on_entry;
    (void)bits_to_clear_on_exit;

    if (harness_event_index >= harness_event_count) {
        longjmp(harness_exit, 1);
    }

    if (harness_result->wait_count < JOB_PIPELINE_HARNESS_MAX_EVENTS) {
        harness_result->wait_ticks[harness_result->wait_count++] = (uint32_t)ticks_to_wait;
    }

    const job_pipeline_harness_event_t *event =
        &harness_events[harness_event_index++];
    harness_now_us += (uint64_t)event->advance_ms * 1000ULL;
    if (event->type == JOB_PIPELINE_HARNESS_NOTIFY) {
        *notification_value = event->slot;
        return pdTRUE;
    }
    return pdFALSE;
}

void spy_asic_send_work(GlobalState *state, const asic_job_t *job)
{
    (void)state;
    if (harness_result->job_count >= JOB_PIPELINE_HARNESS_MAX_JOBS) {
        longjmp(harness_exit, 2);
    }
    if (job != NULL) {
        harness_result->storage[harness_result->job_count] = *job;
        harness_result->jobs[harness_result->job_count] =
            &harness_result->storage[harness_result->job_count];
    } else {
        harness_result->jobs[harness_result->job_count] = NULL;
    }
    harness_result->job_count++;
}

static void spy_asic_set_version_mask(GlobalState *state, uint32_t mask)
{
    (void)state;
    if (harness_result->version_mask_count >= JOB_PIPELINE_HARNESS_MAX_JOBS) {
        longjmp(harness_exit, 2);
    }
    harness_result->version_masks[harness_result->version_mask_count++] = mask;
}

static double stub_asic_get_job_frequency(GlobalState *state, uint32_t version_mask)
{
    (void)state;
    (void)version_mask;
    return harness_job_frequency_ms;
}

static void spy_decode_coinbase(GlobalState *state, const miner_job_t *job)
{
    (void)state;
    (void)job;
    harness_result->coinbase_decode_count++;
}

static void stub_difficulty_controller_update(GlobalState *state)
{
    (void)state;
}

#ifdef xTaskNotifyWait
#undef xTaskNotifyWait
#endif
#define xTaskNotifyWait fake_task_notify_wait
#define esp_timer_get_time fake_esp_timer_get_time
#define ASIC_send_work spy_asic_send_work
#define ASIC_send_job test_asic_send_job
#define ASIC_set_version_mask spy_asic_set_version_mask
#define ASIC_get_asic_job_frequency_ms stub_asic_get_job_frequency
#define SYSTEM_decode_and_apply_coinbase spy_decode_coinbase
#define difficulty_controller_update stub_difficulty_controller_update
#include "../../../main/tasks/create_jobs_task.c"
#undef difficulty_controller_update
#undef SYSTEM_decode_and_apply_coinbase
#undef ASIC_get_asic_job_frequency_ms
#undef ASIC_set_version_mask
#undef ASIC_send_work
#undef ASIC_send_job
#undef esp_timer_get_time
#undef xTaskNotifyWait

void job_pipeline_harness_run(
    job_pipeline_harness_config_t config,
    const job_pipeline_harness_event_t *events, size_t event_count,
    job_pipeline_harness_result_t *result)
{
    if (result == NULL || event_count > JOB_PIPELINE_HARNESS_MAX_EVENTS ||
        (event_count > 0 && events == NULL)) {
        return;
    }

    memset(result, 0, sizeof(*result));
    harness_state = (GlobalState) {
        .DEVICE_CONFIG.family.asic.hw_version_mask = config.hw_version_mask,
        .DEVICE_CONFIG.family.asic.packet_midstates =
            (config.packet_midstates > 0) ? config.packet_midstates : 1,
        .ASIC_initalized = config.asic_initialized,
    };

    harness_events = events;
    harness_event_count = event_count;
    harness_event_index = 0;
    harness_result = result;
    harness_job_frequency_ms = config.job_frequency_ms;
    harness_now_us = 1000000ULL;
    mining_allocator_fault_injector_reset(config.allocation_failure_at);
    mining_hash_fault_injector_reset(config.hash_failure_at);

    int exit_reason = setjmp(harness_exit);
    if (exit_reason == 0) {
        create_jobs_task(&harness_state);
    }

    result->active_job_slot = harness_state.active_job_slot_idx;
    result->allocation_count = mining_allocator_fault_injector_calls();
    result->hash_abort_count = mining_hash_fault_injector_abort_calls();
    harness_events = NULL;
    harness_event_count = 0;
    harness_event_index = 0;
    harness_result = NULL;
    mining_allocator_fault_injector_reset(0);
    mining_hash_fault_injector_reset(0);
}

void job_pipeline_harness_result_free(job_pipeline_harness_result_t *result)
{
    if (result == NULL) return;
    for (size_t index = 0; index < result->job_count; ++index) {
        result->jobs[index] = NULL;
    }
    result->job_count = 0;
}

void job_pipeline_harness_send_job(const asic_job_t *job, job_pipeline_harness_result_t *result)
{
    memset(result, 0, sizeof(*result));
    harness_state = (GlobalState) {0};
    harness_result = result;
    test_asic_send_job(&harness_state, job);
    harness_result = NULL;
}
