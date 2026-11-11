#include <sys/time.h>
#include <limits.h>
#include <inttypes.h>

#include "global_state.h"
#include "esp_log.h"
#include "mining.h"
#include "miner_job.h"
#include "string.h"
#include "esp_timer.h"

#include "asic.h"
#include "difficulty_controller.h"
#include "system.h"
#include "utils.h"

static const char *TAG = "create_jobs_task";

static void send_asic_job(GlobalState *GLOBAL_STATE, const miner_job_t *miner_job,
                          const asic_job_t *job)
{
    if (!GLOBAL_STATE->ASIC_initalized) {
        ESP_LOGW(TAG, "ASIC not initialized, skipping job send");
        return;
    }

    asic_job_t next_job = *job;
    next_job.ntime = miner_job->ntime;
    next_job.nbits = miner_job->nbits;
    memcpy(next_job.pool_target, miner_job->pool_target, sizeof(next_job.pool_target));

    ASIC_send_job(GLOBAL_STATE, &next_job);
}

void create_jobs_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;

    // active_jobs slots are zeroed by SYSTEM_init_system(),
    // before any task that touches them can run.

    asic_job_t active_asic_job;
    miner_job_t *current_work = NULL;
    uint64_t extranonce_2 = 0;
    uint32_t current_version_mask = 0;
    uint32_t versions_rolled = 0;
    bool current_work_sent = false;
    bool need_build = true;
    int timeout_ms = ASIC_get_asic_job_frequency_ms(GLOBAL_STATE);

    ESP_LOGI(TAG, "ASIC Job Interval: %d ms", timeout_ms);
    ESP_LOGI(TAG, "ASIC Ready!");

    while (1) {
        uint64_t start_time = esp_timer_get_time();
        uint32_t slot_notify = 0;
        TickType_t wait_ticks = (timeout_ms > 0) ? pdMS_TO_TICKS(timeout_ms) : 0;
        BaseType_t notified = xTaskNotifyWait(0, ULONG_MAX, &slot_notify, wait_ticks);
        timeout_ms -= (esp_timer_get_time() - start_time) / 1000;

        if (notified == pdTRUE) {
            miner_job_t *new_work = miner_job_get_slot((size_t)slot_notify);
            ESP_LOGI(TAG, "New Work Activated %s (type %d)", new_work->job_id, new_work->type);
            current_work = new_work;
            GLOBAL_STATE->has_work = true;
            GLOBAL_STATE->active_job_slot_idx = (uint8_t)(slot_notify % MINER_JOB_POOL_SIZE);
            current_work_sent = false;
            versions_rolled = 0;
            extranonce_2 = 0;
            need_build = true;

            if (new_work->version_mask != current_version_mask && GLOBAL_STATE->ASIC_initalized) {
                ESP_LOGI(TAG, "Set chip version rolls %i", (int)(new_work->version_mask >> 13));
                ASIC_set_version_mask(GLOBAL_STATE, new_work->version_mask);
                current_version_mask = new_work->version_mask;
            }

            if (GLOBAL_STATE->ASIC_initalized) {
                difficulty_controller_update(GLOBAL_STATE);
            }

            if (!current_work->clean_jobs) {
                // Staged job for next cycle, let current ASIC cycle finish
                continue;
            }
        } else {
            if (current_work == NULL || !GLOBAL_STATE->has_work) {
                current_work = NULL;
                current_work_sent = false;
                vTaskDelay(100 / portTICK_PERIOD_MS);
                continue;
            }
            if (!miner_job_is_rollable(current_work) && current_work_sent && GLOBAL_STATE->DEVICE_CONFIG.family.asic.hardware_version_rolling) {
                timeout_ms = ASIC_get_asic_job_frequency_ms(GLOBAL_STATE);
                continue;
            }
        }

        bool built = false;
        if (need_build) {
            if (mining_build_asic_job(current_work, extranonce_2, current_work->version, &active_asic_job)) {
                built = true;
                need_build = false;
            } else {
                ESP_LOGE(TAG, "Failed to build ASIC job");
            }
        } else {
            built = true;
        }

        if (built) {
            send_asic_job(GLOBAL_STATE, current_work, &active_asic_job);
        }

        if (!current_work_sent) {
            SYSTEM_decode_and_apply_coinbase(GLOBAL_STATE, current_work);
        }
        current_work_sent = true;

        bool advance_extranonce = miner_job_is_rollable(current_work);

        if (built && !GLOBAL_STATE->DEVICE_CONFIG.family.asic.hardware_version_rolling) {
            uint32_t mask = current_work->version_mask;
            if (mask == 0 && !miner_job_is_rollable(current_work)) {
                mask = BIP320_VERSION_ROLLING_MASK;
            }
            if (mask != 0) {
                uint8_t midstates = GLOBAL_STATE->DEVICE_CONFIG.family.asic.software_midstates;
                for (int i = 0; i < midstates; i++) {
                    active_asic_job.version = increment_bitmask(active_asic_job.version, mask);
                }
                versions_rolled += midstates;

                int pop = __builtin_popcount(mask);
                uint32_t max_versions = (pop >= 31) ? UINT32_MAX : (1U << pop);
                if (versions_rolled < max_versions) {
                    advance_extranonce = false;
                } else {
                    versions_rolled = 0;
                    active_asic_job.version = current_work->version;
                }
            }
        }

        if (advance_extranonce) {
            extranonce_2++;
            need_build = true;
        }
        timeout_ms = ASIC_get_asic_job_frequency_ms(GLOBAL_STATE);
    }
}
