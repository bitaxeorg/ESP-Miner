#include "asic.h"
#include "bm_job.h"
#include "global_state.h"
#include "esp_log.h"
#include <stdlib.h>

void ASIC_send_job(GlobalState *state, const asic_job_t *job)
{
    bm_job *converted = malloc(sizeof(*converted));
    if (converted == NULL) {
        ESP_LOGE("asic", "Failed to allocate Bitmain work");
        return;
    }
    if (!bm_job_build_from_asic_job(job,
            state->DEVICE_CONFIG.family.asic.software_midstates, converted)) {
        ESP_LOGE("asic", "Failed to convert common work");
        free(converted);
        return;
    }
    ASIC_send_work(state, converted);
}
