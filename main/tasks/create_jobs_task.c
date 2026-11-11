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

#define MIN_JOB_RUNTIME_MS 1000

typedef struct {
    asic_job_t active_asic_job;
    miner_job_t *current_work;
    uint64_t extranonce_2;
    uint32_t current_version_mask;
    uint32_t sw_versions_rolled;
    uint64_t last_send_time_us;
    uint64_t last_cycle_time_us;
    bool current_work_sent;
    bool need_build;
    bool exhausted;
} job_scheduler_state_t;

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

static TickType_t compute_wait_ticks(const GlobalState *GLOBAL_STATE,
                                     const job_scheduler_state_t *sched,
                                     int job_interval_ms,
                                     uint64_t now_us)
{
    if (sched->current_work == NULL || !GLOBAL_STATE->has_work || sched->exhausted) {
        return portMAX_DELAY;
    }

    if (!sched->current_work_sent) {
        if (sched->last_send_time_us == 0 || job_interval_ms <= 0) {
            return 0;
        }
        int throttle_ms = (job_interval_ms < MIN_JOB_RUNTIME_MS) ? job_interval_ms : MIN_JOB_RUNTIME_MS;
        int64_t elapsed_ms = (int64_t)((now_us - sched->last_send_time_us) / 1000ULL);
        int64_t remaining_ms = (int64_t)throttle_ms - elapsed_ms;
        return (remaining_ms > 0) ? pdMS_TO_TICKS((uint32_t)remaining_ms) : 0;
    }

    if (job_interval_ms <= 0) {
        return 0;
    }
    int64_t elapsed_ms = (int64_t)((now_us - sched->last_cycle_time_us) / 1000ULL);
    int64_t remaining_ms = (int64_t)job_interval_ms - elapsed_ms;
    return (remaining_ms > 0) ? pdMS_TO_TICKS((uint32_t)remaining_ms) : 0;
}

static void advance_work_cursor(job_scheduler_state_t *sched,
                                uint32_t hw_version_mask,
                                uint8_t packet_midstates,
                                bool built)
{
    uint32_t sw_mask = sched->current_work->version_mask & ~hw_version_mask;
    uint8_t step = (packet_midstates > 0) ? packet_midstates : 1;

    if (built && sw_mask != 0) {
        for (uint8_t i = 0; i < step; i++) {
            sched->active_asic_job.version = increment_bitmask(sched->active_asic_job.version, sw_mask);
        }
        sched->sw_versions_rolled += step;

        int pop = __builtin_popcount(sw_mask);
        uint64_t max_versions = UINT64_C(1) << pop;
        if ((uint64_t)sched->sw_versions_rolled < max_versions) {
            return;
        }

        sched->sw_versions_rolled = 0;
        sched->active_asic_job.version = sched->current_work->version;
    }

    if (miner_job_is_rollable(sched->current_work)) {
        sched->extranonce_2++;
        sched->need_build = true;
    } else if (built) {
        sched->exhausted = true;
    }
}

void create_jobs_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;

    // active_jobs slots are zeroed by SYSTEM_init_system(),
    // before any task that touches them can run.

    job_scheduler_state_t sched = {
        .need_build = true,
    };

    int initial_interval_ms = (int)ASIC_get_asic_job_frequency_ms(GLOBAL_STATE, BIP320_VERSION_ROLLING_MASK);
    ESP_LOGI(TAG, "ASIC Job Interval: %d ms", initial_interval_ms);
    ESP_LOGI(TAG, "ASIC Ready!");

    while (1) {
        uint32_t active_mask = (sched.current_work != NULL)
                               ? (sched.current_work_sent ? sched.current_work->version_mask : sched.current_version_mask)
                               : BIP320_VERSION_ROLLING_MASK;
        int job_interval_ms = (int)ASIC_get_asic_job_frequency_ms(GLOBAL_STATE, active_mask);

        uint64_t wait_start_us = esp_timer_get_time();
        TickType_t wait_ticks = compute_wait_ticks(GLOBAL_STATE, &sched, job_interval_ms, wait_start_us);
        uint32_t slot_notify = 0;
        BaseType_t notified = xTaskNotifyWait(0, ULONG_MAX, &slot_notify, wait_ticks);

        if (notified == pdTRUE) {
            bool asic_working = (sched.current_work != NULL) && GLOBAL_STATE->has_work && (sched.last_send_time_us != 0);
            if (!asic_working) {
                sched.last_send_time_us = 0;
            }

            miner_job_t *new_work = miner_job_get_slot((size_t)slot_notify);
            ESP_LOGI(TAG, "New Work Activated %s (type %d)", new_work->job_id, new_work->type);
            sched.current_work = new_work;
            GLOBAL_STATE->has_work = true;
            GLOBAL_STATE->active_job_slot_idx = (uint8_t)(slot_notify % MINER_JOB_POOL_SIZE);
            sched.current_work_sent = false;
            sched.sw_versions_rolled = 0;
            sched.extranonce_2 = 0;
            sched.need_build = true;
            sched.exhausted = false;

            if (!new_work->clean_jobs && asic_working && job_interval_ms > 0) {
                int throttle_ms = (job_interval_ms < MIN_JOB_RUNTIME_MS) ? job_interval_ms : MIN_JOB_RUNTIME_MS;
                int64_t elapsed_ms = (int64_t)((esp_timer_get_time() - sched.last_send_time_us) / 1000ULL);
                if (elapsed_ms < (int64_t)throttle_ms) {
                    continue;
                }
            }
        } else {
            if (sched.current_work == NULL || !GLOBAL_STATE->has_work) {
                sched.current_work = NULL;
                sched.current_work_sent = false;
                sched.exhausted = false;
                sched.last_send_time_us = 0;
                continue;
            }
            if (sched.exhausted) {
                continue;
            }
        }

        if (!sched.current_work_sent && GLOBAL_STATE->ASIC_initalized) {
            if (sched.current_work->version_mask != sched.current_version_mask) {
                uint32_t hw_mask = sched.current_work->version_mask & GLOBAL_STATE->DEVICE_CONFIG.family.asic.hw_version_mask;
                ESP_LOGI(TAG, "Set chip version rolls %i", (int)(hw_mask >> 13));
                ASIC_set_version_mask(GLOBAL_STATE, sched.current_work->version_mask);
                sched.current_version_mask = sched.current_work->version_mask;
            }
            difficulty_controller_update(GLOBAL_STATE);
        }

        bool built = false;
        if (sched.need_build) {
            if (mining_build_asic_job(sched.current_work, sched.extranonce_2, sched.current_work->version, &sched.active_asic_job)) {
                built = true;
                sched.need_build = false;
            } else {
                ESP_LOGE(TAG, "Failed to build ASIC job");
            }
        } else {
            built = true;
        }

        if (built) {
            send_asic_job(GLOBAL_STATE, sched.current_work, &sched.active_asic_job);
        }

        uint64_t cycle_done_us = esp_timer_get_time();
        sched.last_cycle_time_us = cycle_done_us;
        if (built && GLOBAL_STATE->ASIC_initalized) {
            sched.last_send_time_us = cycle_done_us;
        }

        if (!sched.current_work_sent) {
            SYSTEM_decode_and_apply_coinbase(GLOBAL_STATE, sched.current_work);
        }
        sched.current_work_sent = true;

        advance_work_cursor(&sched,
                            GLOBAL_STATE->DEVICE_CONFIG.family.asic.hw_version_mask,
                            GLOBAL_STATE->DEVICE_CONFIG.family.asic.packet_midstates,
                            built);
    }
}
