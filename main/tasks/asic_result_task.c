#include "global_state.h"
#include "work_queue.h"
#include "serial.h"
#include "bm1397.h"
#include <string.h>
#include "esp_log.h"
#include "nvs_config.h"

const char * TAG = "asic_result";

void ASIC_result_task(void * pvParameters)
{

    GlobalState *GLOBAL_STATE = (GlobalState*) pvParameters;

    uint8_t buf[CHUNK_SIZE];
    memset(buf, 0, 1024);
    SERIAL_clear_buffer();
    uint32_t prev_nonce = 0;


    char * user = nvs_config_get_string(NVS_CONFIG_STRATUM_USER, STRATUM_USER);

    while(1){

        //wait for a response, wait time is pretty arbitrary
        int received = SERIAL_rx(buf, 9, 60000);

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
        double nonce_diff = test_nonce_value(
            GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[rx_job_id],
            nonce.nonce,
            rx_midstate_index
        );

        ESP_LOGI(TAG, "Nonce difficulty %.2f of %ld.", nonce_diff, GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[rx_job_id]->pool_diff);

        if (nonce_diff > GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[rx_job_id]->pool_diff)
        {
            SYSTEM_notify_found_nonce(
                &GLOBAL_STATE->SYSTEM_MODULE,
                GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[rx_job_id]->pool_diff,
                nonce_diff,
                GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[rx_job_id]->target
            );

            uint32_t rolled_version = GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[rx_job_id]->version;
            for (int i = 0; i < rx_midstate_index; i++) {
                rolled_version = increment_bitmask(rolled_version, GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[rx_job_id]->version_mask);
            }

            STRATUM_V1_submit_share(
                GLOBAL_STATE->sock,
                user,
                GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[rx_job_id]->jobid,
                GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[rx_job_id]->extranonce2,
                GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[rx_job_id]->ntime,
                nonce.nonce,
                rolled_version ^ GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[rx_job_id]->version
            );
        }

    }

}