#include "global_state.h"
#include "work_queue.h"
#include "serial.h"
#include "bm1397.h"
#include <string.h>
#include "esp_log.h"

#include "driver/i2c.h"

static const char *TAG = "ASIC_task";

// static bm_job ** active_jobs; is required to keep track of the active jobs since the

void ASIC_task(void *pvParameters)
{

    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;

    GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs = malloc(sizeof(bm_job *) * 128);
    GLOBAL_STATE->valid_jobs = malloc(sizeof(uint8_t) * 128);
    for (int i = 0; i < 128; i++)
    {

        GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[i] = NULL;
        GLOBAL_STATE->valid_jobs[i] = 0;
    }

    int baud = (*GLOBAL_STATE->ASIC_functions.set_max_baud_fn)();
    vTaskDelay(10 / portTICK_PERIOD_MS);
    SERIAL_set_baud(baud);

    SYSTEM_notify_mining_started(&GLOBAL_STATE->SYSTEM_MODULE);
    ESP_LOGI(TAG, "ASIC Ready!");
    while (1)
    {

        bm_job *next_bm_job = (bm_job *)queue_dequeue(&GLOBAL_STATE->ASIC_jobs_queue);

        if (next_bm_job->pool_diff != GLOBAL_STATE->stratum_difficulty)
        {
            // ESP_LOGI(TAG, "New difficulty %d", next_bm_job->pool_diff);
            (*GLOBAL_STATE->ASIC_functions.set_difficulty_mask_fn)(next_bm_job->pool_diff);
            GLOBAL_STATE->stratum_difficulty = next_bm_job->pool_diff;
        }

        (*GLOBAL_STATE->ASIC_functions.send_work_fn)(GLOBAL_STATE, next_bm_job); // send the job to the ASIC

        // Time to execute the above code is ~0.3ms
        // vTaskDelay((BM1397_FULLSCAN_MS - 0.3 ) / portTICK_PERIOD_MS);
        vTaskDelay((GLOBAL_STATE->asic_job_frequency_ms - 0.3) / portTICK_PERIOD_MS);
    }
}
