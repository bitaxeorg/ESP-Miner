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

    while (1)
    {
        // Check if ASIC is initialized before trying to process work
        if (!GLOBAL_STATE->ASIC_initalized) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }
        
        //task_result *asic_result = (*GLOBAL_STATE->ASIC_functions.receive_result_fn)(GLOBAL_STATE);
        task_result *asic_result = ASIC_process_work(GLOBAL_STATE);

        if (asic_result == NULL)
        {
            continue;
        }

        if (asic_result->register_type != REGISTER_INVALID) {
            hashrate_monitor_register_read(GLOBAL_STATE, asic_result->register_type, asic_result->asic_nr, asic_result->value);
            continue;
        }

        uint8_t job_id = asic_result->job_id;

        if (GLOBAL_STATE->valid_jobs[job_id] == 0)
        {
            ESP_LOGW(TAG, "Invalid job nonce found, 0x%02X", job_id);
            continue;
        }

        bm_job *active_job = GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id];
        // check the nonce difficulty
        double nonce_diff = test_nonce_value(active_job, asic_result->nonce, asic_result->rolled_version);

        //log the ASIC response
        ESP_LOGI(TAG, "ID: %s, ASIC nr: %d, ver: %08" PRIX32 " Nonce %08" PRIX32 " diff %.1f of %ld.", active_job->jobid, asic_result->asic_nr, asic_result->rolled_version, asic_result->nonce, nonce_diff, active_job->pool_diff);

        if (nonce_diff >= active_job->pool_diff)
        {
            char * user = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_user : GLOBAL_STATE->SYSTEM_MODULE.pool_user;
            int ret = STRATUM_V1_submit_share(
                GLOBAL_STATE->sock,
                GLOBAL_STATE->send_uid++,
                user,
                active_job->jobid,
                active_job->extranonce2,
                active_job->ntime,
                asic_result->nonce,
                asic_result->rolled_version ^ active_job->version);

            if (ret < 0) {
                ESP_LOGI(TAG, "Unable to write share to socket. Closing connection. Ret: %d (errno %d: %s)", ret, errno, strerror(errno));
                stratum_close_connection(GLOBAL_STATE);
            }
        }

        SYSTEM_notify_found_nonce(GLOBAL_STATE, nonce_diff, job_id);
    }
}
