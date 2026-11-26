#include "bzm/result.h"
#include <stddef.h>

bool bzm_result_to_task(const bzm_result_t *share, task_result *result)
{
    if (share == NULL || result == NULL || !share->job_valid) return false;
    *result = (task_result){
        .job = share->job, .nonce = share->nonce,
        .rolled_version = share->final_version,
        .asic_nr = share->asic_index, .core_id = (uint8_t)share->engine_id,
        .small_core_id = share->micro_job_id, .timestamp_us = share->timestamp_us,
    };
    result->job.ntime = share->final_ntime;
    return true;
}
