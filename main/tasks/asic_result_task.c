#include <lwip/tcpip.h>

#include "system.h"
#include <string.h>
#include <stdlib.h>
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
        // Check if ASIC is initialized before trying to process work
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

        uint8_t job_id = asic_result->job_id;

        // Snapshot the job while holding the lock. The shared slot
        // (ASIC_TASK_MODULE.active_jobs[job_id]) can be freed and reused by
        // BM1370_send_work() while we run the (potentially multi-second, blocking)
        // share submit below; keeping a pointer into it is a use-after-free. The
        // bm_job body is inline and safe to copy by value — deep-copy the two
        // heap-owned strings so the snapshot stays valid after we unlock.
        pthread_mutex_lock(&GLOBAL_STATE->ASIC_TASK_MODULE.valid_jobs_lock);
        bool valid = (GLOBAL_STATE->ASIC_TASK_MODULE.valid_jobs[job_id] != 0) &&
                     (GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id] != NULL);
        if (!valid)
        {
            pthread_mutex_unlock(&GLOBAL_STATE->ASIC_TASK_MODULE.valid_jobs_lock);
            ESP_LOGW(TAG, "Invalid job nonce found, 0x%02X", job_id);
            continue;
        }
        bm_job active_job_snapshot = *GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id];
        active_job_snapshot.jobid = active_job_snapshot.jobid ? strdup(active_job_snapshot.jobid) : NULL;
        active_job_snapshot.extranonce2 = active_job_snapshot.extranonce2 ? strdup(active_job_snapshot.extranonce2) : NULL;
        pthread_mutex_unlock(&GLOBAL_STATE->ASIC_TASK_MODULE.valid_jobs_lock);
        bm_job *active_job = &active_job_snapshot;

        uint8_t hash_result[32] __attribute__((aligned(4)));
        calculate_header_hash(active_job, asic_result->nonce, asic_result->rolled_version, hash_result);

        if (GLOBAL_STATE->SELF_TEST_MODULE.is_active) {
            self_test_record_nonce(GLOBAL_STATE, hash_result);
            free(active_job->jobid);
            free(active_job->extranonce2);
            continue;
        }

        // 1. Bit-exact PoW checks
        bool is_share = uint256_lte(hash_result, active_job->pool_target);
        bool is_block = false;
        if (active_job->target != 0) {
            uint8_t network_target[32] __attribute__((aligned(4)));
            nbits_to_target(active_job->target, network_target);
            is_block = uint256_lte(hash_result, network_target);
        }

        // 2. Submit if it meets share or block target IMMEDIATELY
        if (is_share || is_block) {
            uint64_t sent_time_us = 0;
            int ret = stratum_submit_share(GLOBAL_STATE, active_job, asic_result->nonce, asic_result->rolled_version, &sent_time_us);
            if (ret >= 0 && sent_time_us > 0) {
                float process_time = (sent_time_us - asic_result->timestamp_us) / 1000.0f;
                GLOBAL_STATE->SYSTEM_MODULE.process_time = process_time;
                ESP_LOGI(TAG, "Processing time: %0.1f ms", process_time);
            }
        }

        // 3. Telemetry & Display (deferred after submission)
        double nonce_diff = target_to_diff(hash_result);

        SYSTEM_notify_found_nonce(GLOBAL_STATE, nonce_diff, is_block);

        uint32_t version_bits = asic_result->rolled_version ^ active_job->version;
        scoreboard_add(&GLOBAL_STATE->SYSTEM_MODULE.scoreboard, nonce_diff, active_job->jobid, active_job->extranonce2, active_job->ntime, asic_result->nonce, version_bits);

        double pool_diff = target_to_diff(active_job->pool_target);

        // Log the ASIC response
        ESP_LOGI(TAG, "ID: %s, ASIC nr: %d, Core: %d/%d, ver: %08" PRIX32 " Nonce %08" PRIX32 " diff %.1f of %g.",
                 active_job->jobid, asic_result->asic_nr, asic_result->core_id, asic_result->small_core_id,
                 asic_result->rolled_version, asic_result->nonce, nonce_diff, pool_diff);

        free(active_job->jobid);
        free(active_job->extranonce2);
    }
}
