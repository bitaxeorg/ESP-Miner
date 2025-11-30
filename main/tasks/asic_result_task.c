#include <lwip/tcpip.h>

#include "system.h"
#include "work_queue.h"
#include "serial.h"
#include <string.h>
#include "esp_log.h"
#include "nvs_config.h"
#include "utils.h"
#include "stratum_task.h"
#include "hashrate_monitor_task.h"
#include "asic.h"

static const char *TAG = "asic_result";

void ASIC_result_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;

    task_result result;

    while (1)
    {
        // Check if ASIC is initialized before trying to process work
        if (!GLOBAL_STATE->ASIC_initalized) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        memset(&result, 0, sizeof(result));
        
        if (!ASIC_process_work(GLOBAL_STATE, &result)) {
            continue;
        }

        if (result.register_type != REGISTER_INVALID) {
            hashrate_monitor_register_read(GLOBAL_STATE, result.register_type, result.asic_nr, result.value);
            continue;
        }

        uint8_t job_id = result.job_id;

        if (GLOBAL_STATE->valid_jobs[job_id] == 0)
        {
            ESP_LOGW(TAG, "Invalid job nonce found, 0x%02X", job_id);
            continue;
        }

        bm_job *active_job = GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id];

        // Calculate the actual difficulty of this nonce
        double nonce_diff = test_nonce_value(active_job, result.nonce, result.rolled_version);

        // Only submit shares that meet or exceed the pool difficulty
        if (nonce_diff >= active_job->pool_diff) {
            // Store share ID for response latency tracking
            GLOBAL_STATE->SYSTEM_MODULE.last_share_submit_id = GLOBAL_STATE->send_uid;
            
            int64_t result_submit_time_us;

            int ret = STRATUM_V1_submit_share(
                GLOBAL_STATE->sock,
                GLOBAL_STATE->send_uid++,
                active_job->user,
                active_job->jobid,
                active_job->extranonce2,
                active_job->ntime,
                result.nonce,
                result.rolled_version ^ active_job->version,
                &result_submit_time_us);

            // Store submit time in global state for response latency tracking
            GLOBAL_STATE->SYSTEM_MODULE.last_share_submit_time_us = result_submit_time_us;

            // Calculate and store submit latency using the same timestamps
            uint32_t share_submit_latency_us = result_submit_time_us - result.receive_time_us;
            GLOBAL_STATE->SYSTEM_MODULE.share_submit_latency_us = share_submit_latency_us;

            ESP_LOGI(TAG, "Share submit latency: %u µs", share_submit_latency_us);
            if (ret < 0) {
                ESP_LOGI(TAG, "Unable to write share to socket. Closing connection. Ret: %d (errno %d: %s)", ret, errno, strerror(errno));
                stratum_close_connection(GLOBAL_STATE);
            }
        }

        //log the ASIC response
        ESP_LOGI(TAG, "ID: %s, ASIC nr: %d, ver: %08" PRIX32 " Nonce %08" PRIX32 " diff %.1f of %ld.", active_job->jobid, result.asic_nr, result.rolled_version, result.nonce, nonce_diff, active_job->pool_diff);

        SYSTEM_notify_found_nonce(GLOBAL_STATE, nonce_diff, job_id);
    }
}
