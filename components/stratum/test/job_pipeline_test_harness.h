#ifndef JOB_PIPELINE_TEST_HARNESS_H
#define JOB_PIPELINE_TEST_HARNESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mining.h"

typedef struct GlobalState GlobalState;
void spy_asic_send_work(GlobalState *state, bm_job *job);
void test_asic_send_job(GlobalState *state, const asic_job_t *job);

typedef struct {
    bool hardware_version_rolling;
    uint8_t software_midstates;
    bool asic_initialized;
    int job_frequency_ms;
    size_t allocation_failure_at;
} job_pipeline_harness_config_t;

#define JOB_PIPELINE_HARNESS_MAX_EVENTS 8
#define JOB_PIPELINE_HARNESS_MAX_JOBS 8

typedef enum {
    JOB_PIPELINE_HARNESS_TIMEOUT = 0,
    JOB_PIPELINE_HARNESS_NOTIFY,
} job_pipeline_harness_event_type_t;

typedef struct {
    job_pipeline_harness_event_type_t type;
    uint32_t slot;
} job_pipeline_harness_event_t;

typedef struct {
    bm_job *jobs[JOB_PIPELINE_HARNESS_MAX_JOBS];
    size_t job_count;
    uint32_t version_masks[JOB_PIPELINE_HARNESS_MAX_JOBS];
    size_t version_mask_count;
    size_t coinbase_decode_count;
    size_t delay_count;
    uint8_t active_job_slot;
    size_t allocation_count;
} job_pipeline_harness_result_t;

/*
 * Run the real create_jobs_task() and common-job send adapter against a deterministic event
 * script and recording ASIC boundary. Captured jobs have the same ownership
 * production transfers to ASIC_send_work().
 */
void job_pipeline_harness_run(
    job_pipeline_harness_config_t config,
    const job_pipeline_harness_event_t *events, size_t event_count,
    job_pipeline_harness_result_t *result);

void job_pipeline_harness_result_free(job_pipeline_harness_result_t *result);

void job_pipeline_harness_send_common(uint8_t software_midstates,
    const asic_job_t *job, job_pipeline_harness_result_t *result);

#endif /* JOB_PIPELINE_TEST_HARNESS_H */
