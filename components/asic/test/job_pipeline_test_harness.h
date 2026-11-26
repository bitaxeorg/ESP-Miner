#ifndef JOB_PIPELINE_TEST_HARNESS_H
#define JOB_PIPELINE_TEST_HARNESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "asic_job.h"

typedef struct GlobalState GlobalState;
void spy_asic_send_work(GlobalState *state, const asic_job_t *job);
void test_asic_send_job(GlobalState *state, const asic_job_t *job);

typedef struct {
    uint32_t hw_version_mask;
    uint8_t packet_midstates;
    bool asic_initialized;
    int job_frequency_ms;
    size_t allocation_failure_at;
    size_t hash_failure_at;
} job_pipeline_harness_config_t;

#define JOB_PIPELINE_HARNESS_MAX_EVENTS 16
#define JOB_PIPELINE_HARNESS_MAX_JOBS 16

typedef enum {
    JOB_PIPELINE_HARNESS_TIMEOUT = 0,
    JOB_PIPELINE_HARNESS_NOTIFY,
} job_pipeline_harness_event_type_t;

typedef struct {
    job_pipeline_harness_event_type_t type;
    uint32_t slot;
    uint32_t advance_ms;
} job_pipeline_harness_event_t;

typedef struct {
    asic_job_t storage[JOB_PIPELINE_HARNESS_MAX_JOBS];
    asic_job_t *jobs[JOB_PIPELINE_HARNESS_MAX_JOBS];
    size_t job_count;
    uint32_t version_masks[JOB_PIPELINE_HARNESS_MAX_JOBS];
    size_t version_mask_count;
    uint32_t wait_ticks[JOB_PIPELINE_HARNESS_MAX_EVENTS];
    size_t wait_count;
    size_t coinbase_decode_count;
    uint8_t active_job_slot;
    size_t allocation_count;
    size_t hash_abort_count;
} job_pipeline_harness_result_t;

/* Captured jobs are owned by result; release them with job_pipeline_harness_result_free(). */
void job_pipeline_harness_run(
    job_pipeline_harness_config_t config,
    const job_pipeline_harness_event_t *events, size_t event_count,
    job_pipeline_harness_result_t *result);

void job_pipeline_harness_result_free(job_pipeline_harness_result_t *result);

void job_pipeline_harness_send_job(const asic_job_t *job, job_pipeline_harness_result_t *result);

#endif /* JOB_PIPELINE_TEST_HARNESS_H */
