#include "asic.h"
#include "bm_job.h"
#include "global_state.h"

bool ASIC_get_job_snapshot(GlobalState *state, uint8_t job_id, asic_job_t *job)
{
    if (state == NULL || job == NULL || job_id >= MAX_ASIC_JOBS ||
        state->ASIC_TASK_MODULE.active_jobs == NULL ||
        state->ASIC_TASK_MODULE.valid_jobs == NULL) {
        return false;
    }
    pthread_mutex_lock(&state->ASIC_TASK_MODULE.valid_jobs_lock);
    bool valid = state->ASIC_TASK_MODULE.valid_jobs[job_id] != 0 &&
        bm_job_to_asic_job(state->ASIC_TASK_MODULE.active_jobs[job_id], job);
    pthread_mutex_unlock(&state->ASIC_TASK_MODULE.valid_jobs_lock);
    return valid;
}
