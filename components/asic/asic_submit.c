#include "asic.h"
#include "bm_job.h"
#include "global_state.h"
#include "esp_log.h"
#include <stdlib.h>

void ASIC_send_job(GlobalState *state, const asic_job_t *job)
{
    bm_job *bitmain_job = malloc(sizeof(*bitmain_job));
    if (bitmain_job == NULL) {
        ESP_LOGE("asic", "Failed to allocate Bitmain work");
        return;
    }
    if (!bm_job_build_from_asic_job(job,
            state->DEVICE_CONFIG.family.asic.software_midstates, bitmain_job)) {
        ESP_LOGE("asic", "Failed to build Bitmain job");
        free(bitmain_job);
        return;
    }
    ASIC_send_work(state, bitmain_job);
}
