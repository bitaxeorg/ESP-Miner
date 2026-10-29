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

typedef struct {
    bool valid;
    uint32_t epoch;
    uint32_t version_mask;
} asic_version_mask_cache_t;

static bool generate_work_from_miner_job(GlobalState *GLOBAL_STATE, const miner_job_t *job, uint64_t extranonce_2, uint32_t current_version, uint32_t *dispatch_epoch, asic_version_mask_cache_t *mask_cache)
{
    if (!job) return false;

    asic_job_t next_job;
    uint32_t effective_version = job->version;
    if (!GLOBAL_STATE->DEVICE_CONFIG.family.asic.hardware_version_rolling && !miner_job_is_rollable(job)) {
        effective_version = current_version;
    }
    if (!mining_build_asic_job(job, extranonce_2, effective_version, &next_job)) {
        ESP_LOGE(TAG, "Failed to build ASIC job");
        return false;
    }
    if (!GLOBAL_STATE->ASIC_initalized) {
        ESP_LOGW(TAG, "ASIC not initialized, skipping job send");
        return false;
    }
    *dispatch_epoch = __atomic_load_n(&GLOBAL_STATE->ASIC_work_epoch, __ATOMIC_ACQUIRE);
    // Programming the mask sends a UART command. Cache it for this ASIC epoch.
    // Initialization advances the epoch, including recovery after a reset.
    if (!mask_cache->valid || mask_cache->epoch != *dispatch_epoch ||
        mask_cache->version_mask != job->version_mask) {
        ASIC_set_version_mask(GLOBAL_STATE, job->version_mask);
        mask_cache->version_mask = job->version_mask;
        mask_cache->epoch = *dispatch_epoch;
        mask_cache->valid = true;
    }
    ASIC_send_job(GLOBAL_STATE, &next_job);
    return true;
}

void create_jobs_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;

    // active_jobs slots are zeroed by SYSTEM_init_system(),
    // before any task that touches them can run.

    miner_job_t *current_work = NULL;
    bool current_work_sent = false;
    uint32_t sent_work_epoch = 0;
    asic_version_mask_cache_t mask_cache = {0};
    uint64_t extranonce_2 = 0;
    uint32_t current_version = 0;
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
            GLOBAL_STATE->active_job_slot_idx = (uint8_t)(slot_notify % MINER_JOB_POOL_SIZE);
            current_work_sent = false;
            current_version = new_work->version;


            extranonce_2 = 0;

            if (GLOBAL_STATE->ASIC_initalized) {
                difficulty_controller_update(GLOBAL_STATE);
            }

            if (!current_work->clean_jobs) {
                // Staged job for next cycle, let current ASIC cycle finish
                continue;
            }
        } else {
            if (current_work == NULL) {
                vTaskDelay(100 / portTICK_PERIOD_MS);
                continue;
            }
            if (!miner_job_is_rollable(current_work) && current_work_sent && GLOBAL_STATE->DEVICE_CONFIG.family.asic.hardware_version_rolling &&
                sent_work_epoch == __atomic_load_n(&GLOBAL_STATE->ASIC_work_epoch, __ATOMIC_ACQUIRE)) {
                timeout_ms = ASIC_get_asic_job_frequency_ms(GLOBAL_STATE);
                continue;
            }
        }

        uint32_t dispatch_epoch;
        if (!generate_work_from_miner_job(GLOBAL_STATE, current_work, extranonce_2, current_version, &dispatch_epoch, &mask_cache)) {
            timeout_ms = ASIC_get_asic_job_frequency_ms(GLOBAL_STATE);
            continue;
        }
        if (!current_work_sent) {
            SYSTEM_decode_and_apply_coinbase(GLOBAL_STATE, current_work);
        }
        current_work_sent = true;
        sent_work_epoch = dispatch_epoch;

        if (miner_job_is_rollable(current_work)) {
            extranonce_2++;
        } else if (!GLOBAL_STATE->DEVICE_CONFIG.family.asic.hardware_version_rolling) {
            // Software version rolling for ASICs without hardware version rolling (e.g. BM1397) on SV2 Standard Channel
            uint32_t mask = (current_work->version_mask != 0) ? current_work->version_mask : BIP320_VERSION_ROLLING_MASK;
            uint8_t midstates = GLOBAL_STATE->DEVICE_CONFIG.family.asic.software_midstates;
            for (int i = 0; i < midstates; i++) {
                current_version = increment_bitmask(current_version, mask);
            }
        }
        timeout_ms = ASIC_get_asic_job_frequency_ms(GLOBAL_STATE);
    }
}
