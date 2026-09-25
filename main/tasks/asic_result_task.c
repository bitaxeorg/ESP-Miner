#include <lwip/tcpip.h>

#include "system.h"
#include "esp_log.h"
#include "utils.h"
#include "global_state.h"
#include "mining.h"
#include "stratum_task.h"
#include "hashrate_monitor_task.h"
#include "asic.h"
#include "freertos/task.h"
#include "scoreboard.h"
#include "self_test.h"

static const char *TAG = "asic_result";

void ASIC_result_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;

    while (1)
    {
        if (!GLOBAL_STATE->ASIC_initalized) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        task_result *asic_result = ASIC_process_work(GLOBAL_STATE);

        if (asic_result == NULL)
        {
            continue;
        }

        if (asic_result->register_type != REGISTER_INVALID) {
            hashrate_monitor_register_read(GLOBAL_STATE, asic_result->register_type, asic_result->asic_nr, asic_result->value, asic_result->timestamp_us);
            continue;
        }

        const asic_job_t *job = &asic_result->job;
        double nonce_difficulty = mining_nonce_difficulty(job, asic_result->nonce, asic_result->rolled_version);

        if (GLOBAL_STATE->SELF_TEST_MODULE.is_active) {
            self_test_record_nonce(GLOBAL_STATE, nonce_difficulty);
            continue;
        }

        uint32_t version_bits = asic_result->rolled_version ^ job->version;
        if (job->pool_diff > 0.0 && nonce_difficulty >= job->pool_diff)
        {
            uint64_t sent_time_us = 0;
            int submit_result = stratum_submit_share(GLOBAL_STATE, job, asic_result->nonce, asic_result->rolled_version, &sent_time_us);
            if (submit_result >= 0 && sent_time_us > 0) {
                float process_time = (sent_time_us - asic_result->timestamp_us) / 1000.0f;
                GLOBAL_STATE->SYSTEM_MODULE.process_time = process_time;
                ESP_LOGI(TAG, "Processing time: %0.1f ms", process_time);
            }
        }

        ESP_LOGI(TAG, "ID: %s, ASIC nr: %d, Core: %d/%d, ver: %08" PRIX32 " Nonce %08" PRIX32 " diff %.1f of %g.", job->job_id, asic_result->asic_nr, asic_result->core_id, asic_result->small_core_id, asic_result->rolled_version, asic_result->nonce, nonce_difficulty, job->pool_diff);

        SYSTEM_notify_found_nonce(GLOBAL_STATE, nonce_difficulty, job->nbits);

        scoreboard_add(&GLOBAL_STATE->SYSTEM_MODULE.scoreboard, nonce_difficulty, job->job_id, job->extranonce2, job->ntime, asic_result->nonce, version_bits);
    }
}
