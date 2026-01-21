#include <sys/time.h>
#include <limits.h>

#include "work_queue.h"
#include "global_state.h"
#include "esp_log.h"
#include "esp_system.h"
#include "mining.h"
#include "string.h"
#include "esp_timer.h"

#include "asic.h"
#include "system.h"
#include "esp_heap_caps.h"

static const char *TAG = "create_jobs_task";

static void generate_work(GlobalState *GLOBAL_STATE, mining_notify *notification, uint64_t extranonce_2, uint32_t difficulty);

void create_jobs_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;

    // Initialize ASIC task module (moved from ASIC_task)
    GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs = heap_caps_malloc(sizeof(bm_job *) * 128, MALLOC_CAP_SPIRAM);
    GLOBAL_STATE->valid_jobs = heap_caps_malloc(sizeof(uint8_t) * 128, MALLOC_CAP_SPIRAM);
    for (int i = 0; i < 128; i++) {
        GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[i] = NULL;
        GLOBAL_STATE->valid_jobs[i] = 0;
    }

    uint32_t difficulty = GLOBAL_STATE->pool_difficulty;
    mining_notify *current_mining_notification = NULL;
    uint64_t extranonce_2 = 0;
    int timeout_ms = ASIC_get_asic_job_frequency_ms(GLOBAL_STATE);

    ESP_LOGI(TAG, "ASIC Job Interval: %d ms", timeout_ms);
    ESP_LOGI(TAG, "ASIC Ready!");
    
    while (1) {
        uint64_t start_time = esp_timer_get_time();
        mining_notify *new_mining_notification = (mining_notify *)queue_dequeue_timeout(&GLOBAL_STATE->stratum_queue, timeout_ms);
        timeout_ms -= (esp_timer_get_time() - start_time) / 1000;

        if (new_mining_notification != NULL) {
            if (current_mining_notification != NULL) {
                STRATUM_V1_free_mining_notify(current_mining_notification);
            }

            ESP_LOGI(TAG, "New Work Dequeued %s", new_mining_notification->job_id);

            current_mining_notification = new_mining_notification;

            if (GLOBAL_STATE->new_set_mining_difficulty_msg) {
                ESP_LOGI(TAG, "New pool difficulty %lu", GLOBAL_STATE->pool_difficulty);
                difficulty = GLOBAL_STATE->pool_difficulty;
                GLOBAL_STATE->new_set_mining_difficulty_msg = false;
            }

            if (GLOBAL_STATE->new_stratum_version_rolling_msg && GLOBAL_STATE->ASIC_initalized) {
                ESP_LOGI(TAG, "Set chip version rolls %i", (int)(GLOBAL_STATE->version_mask >> 13));
                ASIC_set_version_mask(GLOBAL_STATE, GLOBAL_STATE->version_mask);
                GLOBAL_STATE->new_stratum_version_rolling_msg = false;
            }

            extranonce_2 = 0;

            if (!current_mining_notification->clean_jobs) {
                continue;
            }
        } else {
            if (current_mining_notification == NULL) {
                vTaskDelay(100 / portTICK_PERIOD_MS);
                continue;
            }
        }

        // Generate and send job (either new work or incremented extranonce_2)
        generate_work(GLOBAL_STATE, current_mining_notification, extranonce_2, difficulty);
        extranonce_2++;
        timeout_ms = ASIC_get_asic_job_frequency_ms(GLOBAL_STATE);
    }
}

static void generate_work(GlobalState *GLOBAL_STATE, mining_notify *notification, uint64_t extranonce_2, uint32_t difficulty)
{
    char extranonce_2_str[GLOBAL_STATE->extranonce_2_len * 2 + 1];
    extranonce_2_generate(extranonce_2, GLOBAL_STATE->extranonce_2_len, extranonce_2_str);

    //print generated extranonce_2
    //ESP_LOGI(TAG, "Generated extranonce_2: %s", extranonce_2_str);

    uint8_t coinbase_tx_hash[32];
    calculate_coinbase_tx_hash(notification->coinbase_1, notification->coinbase_2, GLOBAL_STATE->extranonce_str, extranonce_2_str, coinbase_tx_hash);

    uint8_t merkle_root[32];
    calculate_merkle_root_hash(coinbase_tx_hash, (uint8_t(*)[32])notification->merkle_branches, notification->n_merkle_branches, merkle_root);

    bm_job *next_job = malloc(sizeof(bm_job));

    if (next_job == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for new job");
        return;
    }

    construct_bm_job(notification, merkle_root, GLOBAL_STATE->version_mask, difficulty, next_job);

    next_job->extranonce2 = strdup(extranonce_2_str);
    next_job->jobid = strdup(notification->job_id);
    next_job->version_mask = GLOBAL_STATE->version_mask;

    // Check if ASIC is initialized before trying to send work
    if (!GLOBAL_STATE->ASIC_initalized) {
        ESP_LOGW(TAG, "ASIC not initialized, skipping job send");
        // Clean up the job since we're not sending it
        // Note: This job was never stored in active_jobs, so it's safe to free
        free(next_job->jobid);
        free(next_job->extranonce2);
        free(next_job);
        return;
    }

    // The ASIC send function will store it in active_jobs array
    // Job cleanup will be handled by the ASIC result processing
    ASIC_send_work(GLOBAL_STATE, next_job);
}
