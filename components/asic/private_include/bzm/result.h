#ifndef BZM_RESULT_H
#define BZM_RESULT_H

#include <stdint.h>
#include <stdbool.h>
#include "asic_job.h"
#include "asic_common.h"

typedef uint64_t bzm_work_handle_t;

#define BZM_WORK_HANDLE_INVALID UINT64_MAX


typedef struct {
    // Owned snapshot captured by the decoder under the job-store lock.
    bool job_valid;
    asic_job_t job;
    // Opaque outside the active ASIC-family adapter and its work store.
    bzm_work_handle_t work_handle;
    uint32_t nonce;
    // Exact values mined by the ASIC after all rolling has been resolved.
    uint32_t final_ntime;
    uint32_t final_version;
    // Protocol submission delta, already resolved by the ASIC-family adapter.
    uint32_t version_bits;
    uint64_t timestamp_us;
    // Diagnostics only; these fields are not part of result identity.
    uint8_t asic_index;
    uint8_t core_id;
    uint8_t small_core_id;
    uint16_t engine_id;
    uint8_t sequence_id;
    uint8_t micro_job_id;
    uint32_t generation;
} bzm_result_t;

bool bzm_result_to_task(const bzm_result_t *share, task_result *result);

#endif // BZM_RESULT_H
