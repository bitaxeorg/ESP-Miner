#include "job_pipeline_fixture.h"

#include <setjmp.h>
#include <string.h>

#include "asic.h"
#include "global_state.h"
#include "system.h"

#include "../../../main/tasks/create_jobs_task.h"

static jmp_buf fixture_exit;
static const job_pipeline_fixture_event_t *fixture_events;
static size_t fixture_event_count;
static size_t fixture_event_index;
static job_pipeline_fixture_result_t *fixture_result;
static int fixture_job_frequency_ms;
static GlobalState fixture_state;

#ifdef ESP_PLATFORM
#define FIXTURE_BOUNDARY_LINKAGE static
#define FIXTURE_ASIC_SEND_WORK fixture_asic_send_work
#define FIXTURE_ASIC_SET_VERSION_MASK fixture_asic_set_version_mask
#define FIXTURE_ASIC_GET_JOB_FREQUENCY fixture_asic_get_job_frequency
#define FIXTURE_DECODE_COINBASE fixture_decode_coinbase
#else
#define FIXTURE_BOUNDARY_LINKAGE
#define FIXTURE_ASIC_SEND_WORK ASIC_send_work
#define FIXTURE_ASIC_SET_VERSION_MASK ASIC_set_version_mask
#define FIXTURE_ASIC_GET_JOB_FREQUENCY ASIC_get_asic_job_frequency_ms
#define FIXTURE_DECODE_COINBASE SYSTEM_decode_and_apply_coinbase
#endif

static BaseType_t fixture_task_notify_wait(
    uint32_t bits_to_clear_on_entry, unsigned long bits_to_clear_on_exit,
    uint32_t *notification_value, TickType_t ticks_to_wait)
{
    (void)bits_to_clear_on_entry;
    (void)bits_to_clear_on_exit;
    (void)ticks_to_wait;

    if (fixture_event_index >= fixture_event_count) {
        longjmp(fixture_exit, 1);
    }

    const job_pipeline_fixture_event_t *event =
        &fixture_events[fixture_event_index++];
    if (event->type == JOB_PIPELINE_FIXTURE_NOTIFY) {
        *notification_value = event->slot;
        return pdTRUE;
    }
    return pdFALSE;
}

static void fixture_task_delay(TickType_t ticks)
{
    (void)ticks;
    fixture_result->delay_count++;
}

#ifndef ESP_PLATFORM
BaseType_t xTaskNotifyWait(
    uint32_t bits_to_clear_on_entry, unsigned long bits_to_clear_on_exit,
    uint32_t *notification_value, TickType_t ticks_to_wait)
{
    return fixture_task_notify_wait(
        bits_to_clear_on_entry, bits_to_clear_on_exit, notification_value,
        ticks_to_wait);
}

void vTaskDelay(TickType_t ticks)
{
    fixture_task_delay(ticks);
}
#endif

FIXTURE_BOUNDARY_LINKAGE void FIXTURE_ASIC_SEND_WORK(GlobalState *state,
                                                     bm_job *job)
{
    (void)state;
    if (fixture_result->job_count >= JOB_PIPELINE_FIXTURE_MAX_JOBS) {
        longjmp(fixture_exit, 2);
    }
    fixture_result->jobs[fixture_result->job_count++] = job;
}

FIXTURE_BOUNDARY_LINKAGE void FIXTURE_ASIC_SET_VERSION_MASK(GlobalState *state,
                                                            uint32_t mask)
{
    (void)state;
    if (fixture_result->version_mask_count >= JOB_PIPELINE_FIXTURE_MAX_JOBS) {
        longjmp(fixture_exit, 2);
    }
    fixture_result->version_masks[fixture_result->version_mask_count++] = mask;
}

FIXTURE_BOUNDARY_LINKAGE double FIXTURE_ASIC_GET_JOB_FREQUENCY(
    GlobalState *state)
{
    (void)state;
    return fixture_job_frequency_ms;
}

FIXTURE_BOUNDARY_LINKAGE void FIXTURE_DECODE_COINBASE(
    GlobalState *state, const miner_job_t *job)
{
    (void)state;
    (void)job;
    fixture_result->coinbase_decode_count++;
}

/* ESP-IDF test components cannot attach compile definitions to one source, so
 * compile the task into this test-only translation unit and interpose the two
 * scheduler calls. The host target compiles create_jobs_task.c directly so
 * coverage is attributed to the production path. */
#ifdef ESP_PLATFORM
#ifdef xTaskNotifyWait
#undef xTaskNotifyWait
#endif
#ifdef vTaskDelay
#undef vTaskDelay
#endif
#define xTaskNotifyWait fixture_task_notify_wait
#define vTaskDelay fixture_task_delay
#define ASIC_send_work fixture_asic_send_work
#define ASIC_set_version_mask fixture_asic_set_version_mask
#define ASIC_get_asic_job_frequency_ms fixture_asic_get_job_frequency
#define SYSTEM_decode_and_apply_coinbase fixture_decode_coinbase
#include "../../../main/tasks/create_jobs_task.c"
#undef SYSTEM_decode_and_apply_coinbase
#undef ASIC_get_asic_job_frequency_ms
#undef ASIC_set_version_mask
#undef ASIC_send_work
#undef vTaskDelay
#undef xTaskNotifyWait
#endif

void job_pipeline_fixture_run(
    job_pipeline_fixture_config_t config,
    const job_pipeline_fixture_event_t *events, size_t event_count,
    job_pipeline_fixture_result_t *result)
{
    if (result == NULL || event_count > JOB_PIPELINE_FIXTURE_MAX_EVENTS ||
        (event_count > 0 && events == NULL)) {
        return;
    }

    memset(result, 0, sizeof(*result));
    fixture_state = (GlobalState) {
        .DEVICE_CONFIG.family.asic.hardware_version_rolling =
            config.hardware_version_rolling,
        .DEVICE_CONFIG.family.asic.software_midstates =
            config.software_midstates,
        .ASIC_initalized = config.asic_initialized,
    };

    fixture_events = events;
    fixture_event_count = event_count;
    fixture_event_index = 0;
    fixture_result = result;
    fixture_job_frequency_ms = config.job_frequency_ms;

    int exit_reason = setjmp(fixture_exit);
    if (exit_reason == 0) {
        create_jobs_task(&fixture_state);
    }

    result->active_job_slot = fixture_state.active_job_slot_idx;
    fixture_events = NULL;
    fixture_event_count = 0;
    fixture_event_index = 0;
    fixture_result = NULL;
}

void job_pipeline_fixture_result_free(job_pipeline_fixture_result_t *result)
{
    if (result == NULL) return;
    for (size_t index = 0; index < result->job_count; ++index) {
        free_bm_job(result->jobs[index]);
        result->jobs[index] = NULL;
    }
    result->job_count = 0;
}
