#include "work_queue.h"
#include "global_state.h"
#include "esp_log.h"
#include "esp_system.h"
#include "mining.h"
#include <limits.h>
#include "string.h"

#include <sys/time.h>

static const char *TAG = "create_jobs_task";

#define MAX_EXTRANONCE_2 UINT_MAX
#define TASK_YIELD_THRESHOLD 1000 // Yield after this many iterations
#define QUEUE_LOW_WATER_MARK 10 // Adjust based on your requirements

static void process_mining_job(GlobalState *GLOBAL_STATE, mining_notify *notification);
static bool should_generate_more_work(GlobalState *GLOBAL_STATE);
static void generate_additional_work(GlobalState *GLOBAL_STATE, mining_notify *notification);

void create_jobs_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;

    while (1)
    {
        mining_notify *mining_notification = (mining_notify *)queue_dequeue(&GLOBAL_STATE->stratum_queue);
        if (mining_notification == NULL) {
            ESP_LOGE(TAG, "Failed to dequeue mining notification");
            vTaskDelay(pdMS_TO_TICKS(100)); // Wait a bit before trying again
            continue;
        }
        if (GLOBAL_STATE->new_stratum_version_rolling_msg) {
            ESP_LOGI(TAG, "Set chip version rolls %i", (int)(GLOBAL_STATE->version_mask >> 13));
            (GLOBAL_STATE->ASIC_functions.set_version_mask)(GLOBAL_STATE->version_mask);
            GLOBAL_STATE->new_stratum_version_rolling_msg = false;
        }
        ESP_LOGI(TAG, "New Work Dequeued %s", mining_notification->job_id);

        // Process this job immediately
        process_mining_job(GLOBAL_STATE, mining_notification);

        // Now wait for more work or process additional jobs if needed
        uint32_t iteration_count = 0;
        while (GLOBAL_STATE->stratum_queue.count < 1 && GLOBAL_STATE->abandon_work == 0)
        {
            // Check if we need to generate more work based on the current job
            if (should_generate_more_work(GLOBAL_STATE))
            {
                generate_additional_work(GLOBAL_STATE, mining_notification);
            }
            else
            {
                // If no more work needed, wait a bit before checking again
                vTaskDelay(pdMS_TO_TICKS(100));
            }

            // Yield periodically to prevent starving other tasks
            if (++iteration_count >= TASK_YIELD_THRESHOLD) {
                iteration_count = 0;
                vTaskDelay(1); // Minimal delay, just to yield
            }
        }

        if (GLOBAL_STATE->abandon_work == 1)
        {
            GLOBAL_STATE->abandon_work = 0;
            ASIC_jobs_queue_clear(&GLOBAL_STATE->ASIC_jobs_queue);
            xSemaphoreGive(GLOBAL_STATE->ASIC_TASK_MODULE.semaphore);
        }

        STRATUM_V1_free_mining_notify(mining_notification);
    }
}

static void process_mining_job(GlobalState *GLOBAL_STATE, mining_notify *notification)
{
    char *extranonce_2_str = extranonce_2_generate(0, GLOBAL_STATE->extranonce_2_len);
    if (extranonce_2_str == NULL) {
        ESP_LOGE(TAG, "Failed to generate extranonce_2");
        return;
    }

    char *coinbase_tx = construct_coinbase_tx(notification->coinbase_1, notification->coinbase_2, GLOBAL_STATE->extranonce_str, extranonce_2_str);
    if (coinbase_tx == NULL) {
        ESP_LOGE(TAG, "Failed to construct coinbase_tx");
        free(extranonce_2_str);
        return;
    }

    char *merkle_root = calculate_merkle_root_hash(coinbase_tx, (uint8_t(*)[32])notification->merkle_branches, notification->n_merkle_branches);
    if (merkle_root == NULL) {
        ESP_LOGE(TAG, "Failed to calculate merkle_root");
        free(extranonce_2_str);
        free(coinbase_tx);
        return;
    }

    bm_job next_job = construct_bm_job(notification, merkle_root, GLOBAL_STATE->version_mask);

    bm_job *queued_next_job = malloc(sizeof(bm_job));
    if (queued_next_job == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for queued_next_job");
        free(extranonce_2_str);
        free(coinbase_tx);
        free(merkle_root);
        return;
    }

    memcpy(queued_next_job, &next_job, sizeof(bm_job));
    queued_next_job->extranonce2 = extranonce_2_str; // Transfer ownership
    queued_next_job->jobid = strdup(notification->job_id);
    queued_next_job->version_mask = GLOBAL_STATE->version_mask;

    queue_enqueue(&GLOBAL_STATE->ASIC_jobs_queue, queued_next_job);

    free(coinbase_tx);
    free(merkle_root);

    ESP_LOGI(TAG, "Job processed and queued: %s", notification->job_id);
}

static bool should_generate_more_work(GlobalState *GLOBAL_STATE)
{
    return GLOBAL_STATE->ASIC_jobs_queue.count < QUEUE_LOW_WATER_MARK;
}

static void generate_additional_work(GlobalState *GLOBAL_STATE, mining_notify *notification)
{
    static uint32_t extranonce_2 = 1; // Start from 1 as 0 was used in the initial job

    char *extranonce_2_str = extranonce_2_generate(extranonce_2, GLOBAL_STATE->extranonce_2_len);
    if (extranonce_2_str == NULL) {
        ESP_LOGE(TAG, "Failed to generate extranonce_2");
        return;
    }

    char *coinbase_tx = construct_coinbase_tx(notification->coinbase_1, notification->coinbase_2, GLOBAL_STATE->extranonce_str, extranonce_2_str);
    if (coinbase_tx == NULL) {
        ESP_LOGE(TAG, "Failed to construct coinbase_tx");
        free(extranonce_2_str);
        return;
    }

    char *merkle_root = calculate_merkle_root_hash(coinbase_tx, (uint8_t(*)[32])notification->merkle_branches, notification->n_merkle_branches);
    if (merkle_root == NULL) {
        ESP_LOGE(TAG, "Failed to calculate merkle_root");
        free(extranonce_2_str);
        free(coinbase_tx);
        return;
    }

    bm_job next_job = construct_bm_job(notification, merkle_root, GLOBAL_STATE->version_mask);

    bm_job *queued_next_job = malloc(sizeof(bm_job));
    if (queued_next_job == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for queued_next_job");
        free(extranonce_2_str);
        free(coinbase_tx);
        free(merkle_root);
        return;
    }

    memcpy(queued_next_job, &next_job, sizeof(bm_job));
    queued_next_job->extranonce2 = extranonce_2_str; // Transfer ownership
    queued_next_job->jobid = strdup(notification->job_id);
    queued_next_job->version_mask = GLOBAL_STATE->version_mask;

    queue_enqueue(&GLOBAL_STATE->ASIC_jobs_queue, queued_next_job);

    free(coinbase_tx);
    free(merkle_root);

    extranonce_2++;
    if (extranonce_2 >= MAX_EXTRANONCE_2) {
        extranonce_2 = 1; // Reset to 1 if we've reached the maximum
    }
    // Logging could cause websocket to crash use with caution
    //ESP_LOGI(TAG, "Additional job generated and queued: %s (Extranonce2: %lu)", notification->job_id, (extranonce_2 - 1));
}