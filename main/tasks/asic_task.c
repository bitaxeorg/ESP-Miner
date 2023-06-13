#include "global_state.h"
#include "work_queue.h"
#include "serial.h"
#include "bm1397.h"
#include <string.h>
#include "esp_log.h"

#include "driver/i2c.h"


static const char *TAG = "ASIC_task";

// static bm_job ** active_jobs; is required to keep track of the active jobs since the


void ASIC_task(void * pvParameters)
{

    GlobalState *GLOBAL_STATE = (GlobalState*) pvParameters;


    uint8_t id = 0;

    GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs = malloc(sizeof(bm_job *) * 128);
    GLOBAL_STATE->valid_jobs = malloc(sizeof(uint8_t) * 128);
    for (int i = 0; i < 128; i++) {
		GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[i] = NULL;
        GLOBAL_STATE->valid_jobs[i] = 0;
	}



    int baud = BM1397_set_max_baud();
    SERIAL_set_baud(baud);

    SYSTEM_notify_mining_started(&GLOBAL_STATE->SYSTEM_MODULE);
    ESP_LOGI(TAG, "ASIC Ready!");
    while (1) {

        bm_job * next_bm_job = (bm_job *) queue_dequeue(&GLOBAL_STATE->ASIC_jobs_queue);

        if(next_bm_job->pool_diff != GLOBAL_STATE->stratum_difficulty){
            ESP_LOGI(TAG, "New difficulty %d", next_bm_job->pool_diff);
            BM1397_set_job_difficulty_mask(next_bm_job->pool_diff);
            GLOBAL_STATE->stratum_difficulty = next_bm_job->pool_diff;
        }


        struct job_packet job;
        // max job number is 128
        // there is still some really weird logic with the job id bits for the asic to sort out
        // so we have it limited to 128 and it has to increment by 4
        id = (id + 4) % 128;

        job.job_id = id;
        job.num_midstates = next_bm_job->num_midstates;
        memcpy(&job.starting_nonce, &next_bm_job->starting_nonce, 4);
        memcpy(&job.nbits, &next_bm_job->target, 4);
        memcpy(&job.ntime, &next_bm_job->ntime, 4);
        memcpy(&job.merkle4, next_bm_job->merkle_root + 28, 4);
        memcpy(job.midstate, next_bm_job->midstate, 32);

        if (job.num_midstates == 4)
        {
            memcpy(job.midstate1, next_bm_job->midstate1, 32);
            memcpy(job.midstate2, next_bm_job->midstate2, 32);
            memcpy(job.midstate3, next_bm_job->midstate3, 32);
        }

        if (GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job.job_id] != NULL) {
            free_bm_job(GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job.job_id]);
        }

        GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job.job_id] = next_bm_job;

        pthread_mutex_lock(&GLOBAL_STATE->valid_jobs_lock);
        GLOBAL_STATE-> valid_jobs[job.job_id] = 1;
        pthread_mutex_unlock(&GLOBAL_STATE->valid_jobs_lock);




        BM1397_send_work(&job); //send the job to the ASIC

        //Time to execute the above code is ~0.3ms
        vTaskDelay((BM1397_FULLSCAN_MS - 0.3 ) / portTICK_RATE_MS);

    }
}
