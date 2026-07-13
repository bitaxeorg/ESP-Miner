#include <lwip/tcpip.h>

#include "system.h"
#include "work_queue.h"
#include "serial.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "nvs_config.h"
#include "utils.h"
#include "stratum_v2_task.h"
#include "sv2_protocol.h"
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
        pthread_mutex_lock(&GLOBAL_STATE->valid_jobs_lock);
        bool valid = (GLOBAL_STATE->valid_jobs[job_id] != 0) &&
                     (GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id] != NULL);
        if (!valid)
        {
            pthread_mutex_unlock(&GLOBAL_STATE->valid_jobs_lock);
            ESP_LOGW(TAG, "Invalid job nonce found, 0x%02X", job_id);
            continue;
        }
        bm_job active_job_snapshot = *GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id];
        active_job_snapshot.jobid = active_job_snapshot.jobid ? strdup(active_job_snapshot.jobid) : NULL;
        active_job_snapshot.extranonce2 = active_job_snapshot.extranonce2 ? strdup(active_job_snapshot.extranonce2) : NULL;
        pthread_mutex_unlock(&GLOBAL_STATE->valid_jobs_lock);
        bm_job *active_job = &active_job_snapshot;
        // check the nonce difficulty
        double nonce_diff = test_nonce_value(active_job, asic_result->nonce, asic_result->rolled_version);

        if (GLOBAL_STATE->SELF_TEST_MODULE.is_active) {
            self_test_record_nonce(GLOBAL_STATE, nonce_diff);
            free(active_job->jobid);
            free(active_job->extranonce2);
            continue;
        }

        uint32_t version_bits = asic_result->rolled_version ^ active_job->version;
        if (nonce_diff >= active_job->pool_diff)
        {
            if (GLOBAL_STATE->stratum_protocol == STRATUM_PROTOCOL_V2) {
                // SV2: submit with binary protocol
                int ret;
                uint32_t sv2_job_id = (uint32_t)strtoul(active_job->jobid, NULL, 10);

                if (stratum_v2_is_extended_channel(GLOBAL_STATE)) {
                    sv2_conn_t *conn = GLOBAL_STATE->sv2_conn;
                    // SV2 spec: extranonce_size is the miner's rollable portion.
                    // The pool prepends its extranonce_prefix separately.
                    uint8_t en2_len = conn->extranonce_size;
                    uint8_t extranonce_2[32];
                    hex2bin(active_job->extranonce2, extranonce_2, en2_len);
                    ret = stratum_v2_submit_share_extended(GLOBAL_STATE, sv2_job_id,
                                                           asic_result->nonce,
                                                           active_job->ntime,
                                                           asic_result->rolled_version,
                                                           extranonce_2, en2_len);
                } else {
                    ret = stratum_v2_submit_share(GLOBAL_STATE, sv2_job_id,
                                                   asic_result->nonce,
                                                   active_job->ntime,
                                                   asic_result->rolled_version);
                }

                if (ret < 0) {
                    ESP_LOGW(TAG, "Failed to submit SV2 share (ret=%d, errno=%d: %s)",
                             ret, errno, strerror(errno));
                }
            } else {
                // V1: submit with JSON-RPC
                uint16_t active_idx = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.secondary_pool_index : GLOBAL_STATE->SYSTEM_MODULE.primary_pool_index;
                char * user = GLOBAL_STATE->SYSTEM_MODULE.pools[active_idx].user;

                taskENTER_CRITICAL(&GLOBAL_STATE->stratum_mux);
                esp_transport_handle_t transport = GLOBAL_STATE->transport;
                int uid = GLOBAL_STATE->send_uid++;
                taskEXIT_CRITICAL(&GLOBAL_STATE->stratum_mux);

                if (transport == NULL) {
                    ESP_LOGW(TAG, "No stratum connection, dropping share (job 0x%02X)", job_id);
                } else {
                    uint64_t sent_time_us = 0;
                    int ret = STRATUM_V1_submit_share(
                        transport,
                        uid,
                        user,
                        active_job->jobid,
                        active_job->extranonce2,
                        active_job->ntime,
                        asic_result->nonce,
                        version_bits,
                        &sent_time_us);

                    if (ret < 0) {
                        ESP_LOGW(TAG, "Unable to write share to socket (ret: %d, errno %d: %s)", ret, errno, strerror(errno));
                        // stratum_task recv loop will detect a broken connection on its next read and handle reconnection
                    }

                    float process_time = (sent_time_us - asic_result->timestamp_us) / 1000.0f;
                    GLOBAL_STATE->SYSTEM_MODULE.process_time = process_time;
                    ESP_LOGI(TAG, "Processing time: %0.1f ms", process_time);
                }
            }
        }

        //log the ASIC response
        ESP_LOGI(TAG, "ID: %s, ASIC nr: %d, Core: %d/%d, ver: %08" PRIX32 " Nonce %08" PRIX32 " diff %.1f of %g.", active_job->jobid, asic_result->asic_nr, asic_result->core_id, asic_result->small_core_id, asic_result->rolled_version, asic_result->nonce, nonce_diff, active_job->pool_diff);

        SYSTEM_notify_found_nonce(GLOBAL_STATE, nonce_diff, active_job->target);

        scoreboard_add(&GLOBAL_STATE->SYSTEM_MODULE.scoreboard, nonce_diff, active_job->jobid, active_job->extranonce2, active_job->ntime, asic_result->nonce, version_bits);

        free(active_job->jobid);
        free(active_job->extranonce2);
    }
}
