#include "global_state.h"
#include "work_queue.h"
#include "serial.h"
#include "bm1397.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "ASIC_task";

// static bm_job ** active_jobs; is required to keep track of the active jobs since the
// ASIC may not return the nonce in the same order as the jobs were sent
// it also may return a previous nonce under some circumstances
// so we keep a list of jobs indexed by the job id
static bm_job ** active_jobs;

void ASIC_task(void * pvParameters)
{

    GlobalState *GLOBAL_STATE = (GlobalState*) pvParameters;

    uint8_t buf[CHUNK_SIZE];
    memset(buf, 0, 1024);

    uint8_t id = 0;

    active_jobs = malloc(sizeof(bm_job *) * 128);
    GLOBAL_STATE->valid_jobs = malloc(sizeof(uint8_t) * 128);
    for (int i = 0; i < 128; i++) {
		active_jobs[i] = NULL;
        GLOBAL_STATE->valid_jobs[i] = 0;
	}

    uint32_t prev_nonce = 0;

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

        if (active_jobs[job.job_id] != NULL) {
            free_bm_job(active_jobs[job.job_id]);
        }

        active_jobs[job.job_id] = next_bm_job;

        pthread_mutex_lock(&GLOBAL_STATE->valid_jobs_lock);
        GLOBAL_STATE-> valid_jobs[job.job_id] = 1;
        pthread_mutex_unlock(&GLOBAL_STATE->valid_jobs_lock);

        SERIAL_clear_buffer();
        BM1397_send_work(&job); //send the job to the ASIC

        //wait for a response
        int received = SERIAL_rx(buf, 9, BM1397_FULLSCAN_MS);

        if (received < 0) {
            ESP_LOGI(TAG, "Error in serial RX");
            continue;
        } else if(received == 0){
            // Didn't find a solution, restart and try again
            continue;
        }

        if(received != 9 || buf[0] != 0xAA || buf[1] != 0x55){
            ESP_LOGI(TAG, "Serial RX invalid %i", received);
            ESP_LOG_BUFFER_HEX(TAG, buf, received);
            continue;
        }

        uint8_t nonce_found = 0;
        uint32_t first_nonce = 0;

        struct nonce_response nonce;
        memcpy((void *) &nonce, buf, sizeof(struct nonce_response));

        uint8_t rx_job_id = nonce.job_id & 0xfc;
        uint8_t rx_midstate_index = nonce.job_id & 0x03;

        if (GLOBAL_STATE->valid_jobs[rx_job_id] == 0) {
            ESP_LOGI(TAG, "Invalid job nonce found, id=%d", nonce.job_id);
        }

        // ASIC may return the same nonce multiple times
        // or one that was already found
        // most of the time it behavies however
        if (nonce_found == 0) {
            first_nonce = nonce.nonce;
            nonce_found = 1;
        } else if (nonce.nonce == first_nonce) {
            // stop if we've already seen this nonce
            break;
        }

        if (nonce.nonce == prev_nonce) {
            continue;
        } else {
            prev_nonce = nonce.nonce;
        }

        // check the nonce difficulty
        double nonce_diff = test_nonce_value(active_jobs[rx_job_id], nonce.nonce, rx_midstate_index);

        ESP_LOGI(TAG, "Nonce difficulty %.2f of %d.", nonce_diff, active_jobs[rx_job_id]->pool_diff);

        if (nonce_diff > active_jobs[rx_job_id]->pool_diff)
        {
            SYSTEM_notify_found_nonce(&GLOBAL_STATE->SYSTEM_MODULE, active_jobs[rx_job_id]->pool_diff, nonce_diff, next_bm_job->target);

            uint32_t rolled_version = active_jobs[rx_job_id]->version;
            for (int i = 0; i < rx_midstate_index; i++) {
                rolled_version = increment_bitmask(rolled_version, active_jobs[rx_job_id]->version_mask);
            }

            STRATUM_V1_submit_share(
                GLOBAL_STATE->sock,
                STRATUM_USER,
                active_jobs[rx_job_id]->jobid,
                active_jobs[rx_job_id]->extranonce2,
                active_jobs[rx_job_id]->ntime,
                nonce.nonce,
                rolled_version ^ active_jobs[rx_job_id]->version
            );
        }
    }
}
