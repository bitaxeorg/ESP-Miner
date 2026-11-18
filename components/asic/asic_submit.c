#include "asic.h"
#include "asic_internal.h"
#include "esp_log.h"
#include <string.h>

void ASIC_send_job(GlobalState *state, const asic_job_t *job)
{
    if (job == NULL ||
        memchr(job->job_id, 0, sizeof(job->job_id)) == NULL ||
        memchr(job->extranonce2, 0, sizeof(job->extranonce2)) == NULL) {
        ESP_LOGE("asic", "Invalid common job metadata");
        return;
    }
    ASIC_send_work(state, job);
}
