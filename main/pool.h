#ifndef POOL_H_
#define POOL_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "miner_job.h"
#include "stratum_timing.h"

#define MAX_POOLS 8
#define POOL_JOB_NOTIFY_FLAG (1UL << 30)

typedef struct RejectedReasonStat
{
    char message[64];
    uint32_t count;
} RejectedReasonStat;

typedef struct PoolSession
{
    uint64_t shares_submitted;
    uint64_t shares_accepted;
    uint64_t shares_rejected;
    uint16_t shares_pending;
    uint64_t work_received;
    RejectedReasonStat rejected_reason_stats[10];
    int rejected_reason_stats_count;
    float response_time;
    float response_time_min;
    float response_time_max;
    float last_response_time_min;
    float last_response_time_max;
    uint16_t response_share_batch;
    double pool_difficulty;
    char pool_connection_info[64];
    char pool_error[64];
    char pool_banner[128];
    stratum_timing_tracker_t timing;
    miner_job_pool_t job_pool;
    volatile uint8_t active_job_slot_idx;
} PoolSession;

typedef struct GlobalState GlobalState;

static inline uint32_t POOL_encode_job_notification(uint16_t pool_idx, uint8_t slot)
{
    return POOL_JOB_NOTIFY_FLAG | (((uint32_t)(pool_idx & 0xFF)) << 8) | ((uint32_t)(slot % MINER_JOB_POOL_SIZE));
}

PoolSession *POOL_get_active_session(GlobalState *gs);
miner_job_t *POOL_get_job_slot(GlobalState *gs, uint16_t pool_idx, size_t index);
miner_job_t *POOL_resolve_notified_job(GlobalState *gs, uint32_t notification_value);

void POOL_notify_submitted_share(GlobalState *gs, uint16_t pool_idx);
void POOL_notify_accepted_share(GlobalState *gs, uint16_t pool_idx);
void POOL_notify_rejected_share(GlobalState *gs, uint16_t pool_idx, const char * error_msg);
void POOL_notify_work_received(GlobalState *gs, uint16_t pool_idx);
void POOL_set_difficulty(GlobalState *gs, uint16_t pool_idx, double difficulty);
void POOL_clear_status(GlobalState *gs, uint16_t pool_idx);
void POOL_set_error(GlobalState *gs, uint16_t pool_idx, const char *err_msg);
void POOL_set_connection_info(GlobalState *gs, uint16_t pool_idx, const char *info);
void POOL_set_banner(GlobalState *gs, uint16_t pool_idx, const char *banner);
void POOL_timing_record(GlobalState *gs, uint16_t pool_idx, uint32_t id, uint64_t submit_time_us);
float POOL_timing_calculate_ms(GlobalState *gs, uint16_t pool_idx, uint32_t id, uint64_t now_us);
void POOL_record_response_time(GlobalState *gs, uint16_t pool_idx, float response_time_ms, uint16_t batch_count);
void POOL_reset_session(GlobalState *gs, uint16_t pool_idx);

#endif /* POOL_H_ */
