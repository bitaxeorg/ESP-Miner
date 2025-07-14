#include <lwip/tcpip.h>

#include "system.h"
#include "work_queue.h"
#include <string.h>
#include "esp_log.h"
#include "utils.h"
#include "stratum_task.h"
#include "asic.h"
#include "asic_task.h"
#include "pool_module.h"

static const char *TAG = "asic_result";

void ASIC_result_task(void *pvParameters)
{

    while (1)
    {
        //task_result *asic_result = (*GLOBAL_STATE.ASIC_functions.receive_result_fn)(GLOBAL_STATE);
        task_result *asic_result = ASIC_process_work();

        if (asic_result == NULL)
        {
            continue;
        }

        uint8_t job_id = asic_result->job_id;

        if (ASIC_TASK_MODULE.valid_jobs[job_id] == 0)
        {
            ESP_LOGW(TAG, "Invalid job nonce found, 0x%02X", job_id);
            continue;
        }

        bm_job *active_job = ASIC_TASK_MODULE.active_jobs[job_id];
        // check the nonce difficulty
        double nonce_diff = test_nonce_value(active_job, asic_result->nonce, asic_result->rolled_version);

        //log the ASIC response
        ESP_LOGI(TAG, "ID: %s, ver: %08" PRIX32 " Nonce %08" PRIX32 " diff %.1f of %ld.", active_job->jobid, asic_result->rolled_version, asic_result->nonce, nonce_diff, active_job->pool_diff);

        if (nonce_diff >= active_job->pool_diff)
        {
            char * user = POOL_MODULE.is_using_fallback ? POOL_MODULE.fallback_pool_user : POOL_MODULE.pool_user;
            int ret = STRATUM_V1_submit_share(
                MINING_MODULE.sock,
                MINING_MODULE.send_uid++,
                user,
                active_job->jobid,
                active_job->extranonce2,
                active_job->ntime,
                asic_result->nonce,
                asic_result->rolled_version ^ active_job->version);

            if (ret < 0) {
                ESP_LOGI(TAG, "Unable to write share to socket. Closing connection. Ret: %d (errno %d: %s)", ret, errno, strerror(errno));
                stratum_close_connection();
            }
        }

        SYSTEM_notify_found_nonce(nonce_diff, job_id);
    }
}
