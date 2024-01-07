#include "global_state.h"
#include "work_queue.h"
#include "serial.h"
#include "bm1397.h"
#include <string.h>
#include "esp_log.h"
#include "nvs_config.h"
#include "utils.h"

const char *TAG = "asic_result";

void ASIC_result_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;
    SERIAL_clear_buffer();

    char *user = nvs_config_get_string(NVS_CONFIG_STRATUM_USER, STRATUM_USER);

    while (1)
    {

        task_result *asic_result = (*GLOBAL_STATE->ASIC_functions.receive_result_fn)(GLOBAL_STATE);

        if (asic_result == NULL)
        {
            continue;
        }

        uint8_t job_id = asic_result->job_id;

        if (GLOBAL_STATE->valid_jobs[job_id] == 0)
        {
            ESP_LOGI(TAG, "Invalid job nonce found, id=%d", job_id);
        }

        // check the nonce difficulty
        double nonce_diff = test_nonce_value(
            GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id],
            asic_result->nonce,
            asic_result->rolled_version);

        ESP_LOGI(TAG, "Nonce difficulty %.2f of %ld.", nonce_diff, GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id]->pool_diff);

        if (nonce_diff > GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id]->pool_diff)
        {
            SYSTEM_notify_found_nonce(
                &GLOBAL_STATE->SYSTEM_MODULE,
                GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id]->pool_diff,
                nonce_diff,
                GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id]->target,
                GLOBAL_STATE->POWER_MANAGEMENT_MODULE.power
                );

            STRATUM_V1_submit_share(
                GLOBAL_STATE->sock,
                user,
                GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id]->jobid,
                GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id]->extranonce2,
                GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id]->ntime,
                asic_result->nonce,
                asic_result->rolled_version ^ GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id]->version);
        }
    }
}