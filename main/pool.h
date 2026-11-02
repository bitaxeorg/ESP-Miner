#ifndef POOL_H_
#define POOL_H_

#include <stdint.h>
#include <stdbool.h>
#include "stratum_timing.h"

#define MAX_POOLS 8

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
} PoolSession;

typedef struct GlobalState GlobalState;

PoolSession *POOL_get_active_session(GlobalState *gs);

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
