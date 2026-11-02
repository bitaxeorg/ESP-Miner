#include "pool.h"
#include <string.h>
#include <stdlib.h>
#include "global_state.h"
#include "system.h"
#include "esp_log.h"

static inline void update_pending_shares(PoolSession *session)
{
    if (!session) return;
    uint64_t resolved = session->shares_accepted + session->shares_rejected;
    uint64_t pending = (session->shares_submitted > resolved) ? (session->shares_submitted - resolved) : 0;
    session->shares_pending = (uint16_t)(pending > UINT16_MAX ? UINT16_MAX : pending);
}

static inline PoolSession *get_pool_session(GlobalState *gs, uint16_t pool_idx)
{
    if (!gs || pool_idx >= MAX_POOLS) return NULL;
    return &gs->SYSTEM_MODULE.pool_sessions[pool_idx];
}

PoolSession *POOL_get_active_session(GlobalState *gs)
{
    if (!gs) return NULL;
    uint16_t idx = gs->SYSTEM_MODULE.is_using_fallback 
        ? gs->SYSTEM_MODULE.secondary_pool_index 
        : gs->SYSTEM_MODULE.primary_pool_index;
    return &gs->SYSTEM_MODULE.pool_sessions[(idx < MAX_POOLS) ? idx : 0];
}

void POOL_timing_record(GlobalState *gs, uint16_t pool_idx, uint32_t id, uint64_t submit_time_us)
{
    PoolSession *session = get_pool_session(gs, pool_idx);
    if (!session) return;
    stratum_timing_record(&session->timing, id, submit_time_us);
}

float POOL_timing_calculate_ms(GlobalState *gs, uint16_t pool_idx, uint32_t id, uint64_t now_us)
{
    PoolSession *session = get_pool_session(gs, pool_idx);
    if (!session) return -1.0f;
    return stratum_timing_calculate_ms(&session->timing, id, now_us);
}

void POOL_notify_submitted_share(GlobalState * GLOBAL_STATE, uint16_t pool_idx)
{
    if (!GLOBAL_STATE) return;

    PoolSession * session = get_pool_session(GLOBAL_STATE, pool_idx);
    if (!session) return;
    session->shares_submitted++;
    update_pending_shares(session);
}

void POOL_notify_accepted_share(GlobalState * GLOBAL_STATE, uint16_t pool_idx)
{
    if (!GLOBAL_STATE) return;

    PoolSession * session = get_pool_session(GLOBAL_STATE, pool_idx);
    if (!session) return;
    session->shares_accepted++;
    update_pending_shares(session);
}

void POOL_notify_work_received(GlobalState * GLOBAL_STATE, uint16_t pool_idx)
{
    if (!GLOBAL_STATE) return;

    PoolSession * session = get_pool_session(GLOBAL_STATE, pool_idx);
    if (!session) return;
    session->work_received++;
}

void POOL_set_difficulty(GlobalState * GLOBAL_STATE, uint16_t pool_idx, double difficulty)
{
    if (!GLOBAL_STATE) return;

    PoolSession * session = get_pool_session(GLOBAL_STATE, pool_idx);
    if (!session) return;
    session->pool_difficulty = difficulty;
}

void POOL_clear_status(GlobalState * GLOBAL_STATE, uint16_t pool_idx)
{
    if (!GLOBAL_STATE) return;

    PoolSession * session = get_pool_session(GLOBAL_STATE, pool_idx);
    if (!session) return;
    session->pool_error[0] = '\0';
    session->pool_banner[0] = '\0';
}

void POOL_set_error(GlobalState * GLOBAL_STATE, uint16_t pool_idx, const char * err_msg)
{
    if (!GLOBAL_STATE) return;

    PoolSession * session = get_pool_session(GLOBAL_STATE, pool_idx);
    if (!session) return;
    if (err_msg && err_msg[0] != '\0') {
        strlcpy(session->pool_connection_info, err_msg, sizeof(session->pool_connection_info));
        strlcpy(session->pool_error, err_msg, sizeof(session->pool_error));
    } else {
        session->pool_error[0] = '\0';
    }
}

void POOL_set_connection_info(GlobalState * GLOBAL_STATE, uint16_t pool_idx, const char * info)
{
    if (!GLOBAL_STATE) return;

    PoolSession * session = get_pool_session(GLOBAL_STATE, pool_idx);
    if (!session) return;
    if (info) {
        strlcpy(session->pool_connection_info, info, sizeof(session->pool_connection_info));
    } else {
        session->pool_connection_info[0] = '\0';
    }
}

void POOL_set_banner(GlobalState * GLOBAL_STATE, uint16_t pool_idx, const char * banner)
{
    if (!GLOBAL_STATE) return;

    PoolSession * session = get_pool_session(GLOBAL_STATE, pool_idx);
    if (!session) return;
    if (banner) {
        strlcpy(session->pool_banner, banner, sizeof(session->pool_banner));
    } else {
        session->pool_banner[0] = '\0';
    }
}

static int compare_rejected_reason_stats(const void *a, const void *b) {
    const RejectedReasonStat *ea = a;
    const RejectedReasonStat *eb = b;
    return (eb->count > ea->count) - (ea->count > eb->count);
}

void POOL_notify_rejected_share(GlobalState * GLOBAL_STATE, uint16_t pool_idx, const char * error_msg)
{
    if (!GLOBAL_STATE) return;

    PoolSession * session = get_pool_session(GLOBAL_STATE, pool_idx);
    if (!session) return;
    session->shares_rejected++;
    update_pending_shares(session);

    if (!error_msg) return;

    for (int i = 0; i < session->rejected_reason_stats_count; i++) {
        if (strncmp(session->rejected_reason_stats[i].message, error_msg, sizeof(session->rejected_reason_stats[i].message) - 1) == 0) {
            session->rejected_reason_stats[i].count++;
            return;
        }
    }

    if (session->rejected_reason_stats_count < (int)(sizeof(session->rejected_reason_stats) / sizeof(session->rejected_reason_stats[0]))) {
        strncpy(session->rejected_reason_stats[session->rejected_reason_stats_count].message, 
                error_msg, 
                sizeof(session->rejected_reason_stats[session->rejected_reason_stats_count].message) - 1);
        session->rejected_reason_stats[session->rejected_reason_stats_count].message[sizeof(session->rejected_reason_stats[session->rejected_reason_stats_count].message) - 1] = '\0'; // Ensure null termination
        session->rejected_reason_stats[session->rejected_reason_stats_count].count = 1;
        session->rejected_reason_stats_count++;
    }

    if (session->rejected_reason_stats_count > 1) {
        qsort(session->rejected_reason_stats, session->rejected_reason_stats_count, 
            sizeof(session->rejected_reason_stats[0]), compare_rejected_reason_stats);
    }    
}

void POOL_record_response_time(GlobalState * GLOBAL_STATE, uint16_t pool_idx, float response_time_ms, uint16_t batch_count)
{
    if (!GLOBAL_STATE || pool_idx >= MAX_POOLS || response_time_ms <= 0.0f) return;
    PoolSession *session = get_pool_session(GLOBAL_STATE, pool_idx);
    if (!session) return;
    session->response_time = response_time_ms;
    session->response_share_batch = batch_count;
    if (session->response_time_min <= 0.0f || response_time_ms < session->response_time_min) {
        session->response_time_min = response_time_ms;
    }
    if (session->response_time_max <= 0.0f || response_time_ms > session->response_time_max) {
        session->response_time_max = response_time_ms;
    }
}

void POOL_reset_session(GlobalState * GLOBAL_STATE, uint16_t pool_idx)
{
    if (!GLOBAL_STATE || pool_idx >= MAX_POOLS) return;

    PoolSession *session = get_pool_session(GLOBAL_STATE, pool_idx);
    if (!session) return;
    memset(session, 0, sizeof(*session));

    bool is_active_pool = (!GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback && pool_idx == GLOBAL_STATE->SYSTEM_MODULE.primary_pool_index) ||
                          (GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback && pool_idx == GLOBAL_STATE->SYSTEM_MODULE.secondary_pool_index);
    if (is_active_pool) {
        SYSTEM_clean_jobs_queue(GLOBAL_STATE);
        SYSTEM_reset_coinbase_ui_state(GLOBAL_STATE);
    }
}
