#include <limits.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_system.h"

#include "mining.h"
#include "string.h"
#include "work_queue.h"
#include "freertos/FreeRTOS.h"
#include "asic_task_module.h"
#include "asic.h"
#include "mining_module.h"
#include "pool_module.h"

static const char * TAG = "create_jobs_task";

#define QUEUE_LOW_WATER_MARK 10 // Adjust based on your requirements

static bool should_generate_more_work();
static void generate_work(mining_notify * notification, uint32_t extranonce_2, uint32_t difficulty);

void create_jobs_task(void * pvParameters)
{

    uint32_t difficulty = POOL_MODULE.pool_difficulty;
    while (1) {
        mining_notify * mining_notification = (mining_notify *) queue_dequeue(&MINING_MODULE.stratum_queue);
        if (mining_notification == NULL) {
            ESP_LOGE(TAG, "Failed to dequeue mining notification");
            vTaskDelay(100 / portTICK_PERIOD_MS); // Wait a bit before trying again
            continue;
        }

        ESP_LOGI(TAG, "New Work Dequeued %s", mining_notification->job_id);

        if (MINING_MODULE.new_set_mining_difficulty_msg) {
            ESP_LOGI(TAG, "New pool difficulty %i", POOL_MODULE.pool_difficulty);
            difficulty = POOL_MODULE.pool_difficulty;
        }

        if (MINING_MODULE.new_stratum_version_rolling_msg) {
            ESP_LOGI(TAG, "Set chip version rolls %i", (int)(MINING_MODULE.version_mask >> 13));
            ASIC_set_version_mask(MINING_MODULE.version_mask);
            MINING_MODULE.new_stratum_version_rolling_msg = false;
        }

        uint32_t extranonce_2 = 0;
        while (MINING_MODULE.stratum_queue.count < 1 && MINING_MODULE.abandon_work == 0) {
            if (should_generate_more_work()) {
                generate_work(mining_notification, extranonce_2, difficulty);

                // Increase extranonce_2 for the next job.
                extranonce_2++;
            } else {
                // If no more work needed, wait a bit before checking again.
                vTaskDelay(100 / portTICK_PERIOD_MS);
            }
        }

        if (MINING_MODULE.abandon_work == 1) {
            MINING_MODULE.abandon_work = 0;
            ASIC_jobs_queue_clear(&MINING_MODULE.ASIC_jobs_queue);
            xSemaphoreGive(ASIC_TASK_MODULE.semaphore);
        }

        STRATUM_V1_free_mining_notify(mining_notification);
    }
}

static bool should_generate_more_work()
{
    return MINING_MODULE.ASIC_jobs_queue.count < QUEUE_LOW_WATER_MARK;
}

static void generate_work(mining_notify * notification, uint32_t extranonce_2, uint32_t difficulty)
{
    char * extranonce_2_str = extranonce_2_generate(extranonce_2, MINING_MODULE.extranonce_2_len);
    if (extranonce_2_str == NULL) {
        ESP_LOGE(TAG, "Failed to generate extranonce_2");
        return;
    }

    char * coinbase_tx =
        construct_coinbase_tx(notification->coinbase_1, notification->coinbase_2, MINING_MODULE.extranonce_str, extranonce_2_str);
    if (coinbase_tx == NULL) {
        ESP_LOGE(TAG, "Failed to construct coinbase_tx");
        free(extranonce_2_str);
        return;
    }

    char * merkle_root =
        calculate_merkle_root_hash(coinbase_tx, (uint8_t (*)[32]) notification->merkle_branches, notification->n_merkle_branches);
    if (merkle_root == NULL) {
        ESP_LOGE(TAG, "Failed to calculate merkle_root");
        free(extranonce_2_str);
        free(coinbase_tx);
        return;
    }

    bm_job next_job = construct_bm_job(notification, merkle_root, MINING_MODULE.version_mask, difficulty);

    bm_job * queued_next_job = malloc(sizeof(bm_job));
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
    queued_next_job->version_mask = MINING_MODULE.version_mask;

    queue_enqueue(&MINING_MODULE.ASIC_jobs_queue, queued_next_job);

    free(coinbase_tx);
    free(merkle_root);
}