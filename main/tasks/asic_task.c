#include "system.h"
#include "work_queue.h"
#include "serial.h"
#include <string.h>
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "asic_task.h"
#include "asic.h"
#include "power_management_task.h"

static const char *TAG = "asic_task";

// static bm_job ** active_jobs; is required to keep track of the active jobs since the
const double NONCE_SPACE = 4294967296.0; //  2^32

double ASIC_get_asic_job_frequency_ms(float frequency)
{
    switch (DEVICE_CONFIG.family.asic.id) {
        case BM1397:
            // no version-rolling so same Nonce Space is splitted between Small Cores
            return (NONCE_SPACE / (double) (frequency * DEVICE_CONFIG.family.asic.small_core_count * 1000)) / (double) DEVICE_CONFIG.family.asic_count;
        case BM1366:
            return 2000;
        default:
            return 500;
    }
}

void ASIC_task(void *pvParameters)
{
    

    //initialize the semaphore
    ASIC_TASK_MODULE.semaphore = xSemaphoreCreateBinary();

    ASIC_TASK_MODULE.active_jobs = malloc(sizeof(bm_job *) * 128);
    ASIC_TASK_MODULE.valid_jobs = malloc(sizeof(uint8_t) * 128);
    for (int i = 0; i < 128; i++)
    {
        ASIC_TASK_MODULE.active_jobs[i] = NULL;
        ASIC_TASK_MODULE.valid_jobs[i] = 0;
    }

    double asic_job_frequency_ms = ASIC_get_asic_job_frequency_ms(POWER_MANAGEMENT_MODULE.frequency_value);

    ESP_LOGI(TAG, "ASIC Job Interval: %.2f ms", asic_job_frequency_ms);
    SYSTEM_notify_mining_started();
    ESP_LOGI(TAG, "ASIC Ready!");

    while (1)
    {
        bm_job *next_bm_job = (bm_job *)queue_dequeue(&GLOBAL_STATE.ASIC_jobs_queue);

        //(*GLOBAL_STATE.ASIC_functions.send_work_fn)(next_bm_job); // send the job to the ASIC
        ASIC_send_work(next_bm_job);

        // Time to execute the above code is ~0.3ms
        // Delay for ASIC(s) to finish the job
        //vTaskDelay((asic_job_frequency_ms - 0.3) / portTICK_PERIOD_MS);
        xSemaphoreTake(ASIC_TASK_MODULE.semaphore, asic_job_frequency_ms / portTICK_PERIOD_MS);
    }
}
