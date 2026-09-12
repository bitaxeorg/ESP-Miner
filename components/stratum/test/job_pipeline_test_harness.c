#include "mining_test_bindings.h"
#include "job_pipeline_test_harness.h"

#include <setjmp.h>
#include <string.h>

#include "asic.h"
#include "global_state.h"
#include "system.h"

#include "../../../main/tasks/create_jobs_task.h"

static jmp_buf harness_exit;
static const job_pipeline_harness_event_t *harness_events;
static size_t harness_event_count;
static size_t harness_event_index;
static job_pipeline_harness_result_t *harness_result;
static int harness_job_frequency_ms;
static GlobalState harness_state;

static BaseType_t fake_task_notify_wait(
    uint32_t bits_to_clear_on_entry, unsigned long bits_to_clear_on_exit,
    uint32_t *notification_value, TickType_t ticks_to_wait)
{
    (void)bits_to_clear_on_entry;
    (void)bits_to_clear_on_exit;
    (void)ticks_to_wait;

    if (harness_event_index >= harness_event_count) {
        longjmp(harness_exit, 1);
    }

    const job_pipeline_harness_event_t *event =
        &harness_events[harness_event_index++];
    if (event->type == JOB_PIPELINE_HARNESS_NOTIFY) {
        *notification_value = event->slot;
        return pdTRUE;
    }
    return pdFALSE;
}

static void spy_task_delay(TickType_t ticks)
{
    (void)ticks;
    harness_result->delay_count++;
}

void spy_asic_send_work(GlobalState *state, bm_job *job)
{
    (void)state;
    if (harness_result->job_count >= JOB_PIPELINE_HARNESS_MAX_JOBS) {
        longjmp(harness_exit, 2);
    }
    harness_result->jobs[harness_result->job_count++] = job;
}

static void spy_asic_set_version_mask(GlobalState *state, uint32_t mask)
{
    (void)state;
    if (harness_result->version_mask_count >= JOB_PIPELINE_HARNESS_MAX_JOBS) {
        longjmp(harness_exit, 2);
    }
    harness_result->version_masks[harness_result->version_mask_count++] = mask;
}

static double stub_asic_get_job_frequency(GlobalState *state)
{
    (void)state;
    return harness_job_frequency_ms;
}

static void spy_decode_coinbase(GlobalState *state, const miner_job_t *job)
{
    (void)state;
    (void)job;
    harness_result->coinbase_decode_count++;
}

/* Test components cannot attach compile definitions to one source, so compile
 * the task into this test-only translation unit and interpose its boundaries. */
#ifdef xTaskNotifyWait
#undef xTaskNotifyWait
#endif
#ifdef vTaskDelay
#undef vTaskDelay
#endif
#define xTaskNotifyWait fake_task_notify_wait
#define vTaskDelay spy_task_delay
#define ASIC_send_work spy_asic_send_work
#define ASIC_send_job test_asic_send_job
#define ASIC_set_version_mask spy_asic_set_version_mask
#define ASIC_get_asic_job_frequency_ms stub_asic_get_job_frequency
#define SYSTEM_decode_and_apply_coinbase spy_decode_coinbase
#include "../../../main/tasks/create_jobs_task.c"
#undef SYSTEM_decode_and_apply_coinbase
#undef ASIC_get_asic_job_frequency_ms
#undef ASIC_set_version_mask
#undef ASIC_send_work
#undef ASIC_send_job
#undef vTaskDelay
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
        .DEVICE_CONFIG.family.asic.hardware_version_rolling =
            config.hardware_version_rolling,
        .DEVICE_CONFIG.family.asic.software_midstates =
            config.software_midstates,
        .ASIC_initalized = config.asic_initialized,
    };

    harness_events = events;
    harness_event_count = event_count;
    harness_event_index = 0;
    harness_result = result;
    harness_job_frequency_ms = config.job_frequency_ms;
    mining_allocator_fault_injector_reset(config.allocation_failure_at);

    int exit_reason = setjmp(harness_exit);
    if (exit_reason == 0) {
        create_jobs_task(&harness_state);
    }

    result->active_job_slot = harness_state.active_job_slot_idx;
    result->allocation_count = mining_allocator_fault_injector_calls();
    harness_events = NULL;
    harness_event_count = 0;
    harness_event_index = 0;
    harness_result = NULL;
    mining_allocator_fault_injector_reset(0);
}

void job_pipeline_harness_result_free(job_pipeline_harness_result_t *result)
{
    if (result == NULL) return;
    for (size_t index = 0; index < result->job_count; ++index) {
        free_bm_job(result->jobs[index]);
        result->jobs[index] = NULL;
    }
    result->job_count = 0;
}

void job_pipeline_harness_send_common(uint8_t software_midstates,
    const asic_job_t *job, job_pipeline_harness_result_t *result)
{
    memset(result, 0, sizeof(*result));
    harness_state = (GlobalState) {
        .DEVICE_CONFIG.family.asic.software_midstates = software_midstates,
    };
    harness_result = result;
    test_asic_send_job(&harness_state, job);
    harness_result = NULL;
}
