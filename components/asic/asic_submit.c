#include "asic.h"
#include "asic_internal.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

void ASIC_send_job(GlobalState *state, const asic_job_t *job)
{
    if (job == NULL ||
        memchr(job->job_id, 0, sizeof(job->job_id)) == NULL ||
        memchr(job->extranonce2, 0, sizeof(job->extranonce2)) == NULL) {
        ESP_LOGE("asic", "Invalid common job metadata");
        return;
    }
    asic_job_t *retained_job = malloc(sizeof(*retained_job));
    if (retained_job == NULL) {
        ESP_LOGE("asic", "Failed to allocate common job");
        return;
    }
    *retained_job = *job;
    ASIC_send_work(state, retained_job);
}
