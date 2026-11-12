#include "mining_watchdog.h"

#include <math.h>
#include <string.h>

void wd_asic_init(wd_asic_t *wd, uint64_t now_ms, uint8_t recoveries, wd_asic_action_t last_action)
{
    memset(wd, 0, sizeof(*wd));
    wd->last_ms = now_ms;
    wd->recoveries = recoveries;
    wd->last_recovery_ms = now_ms;
    wd->last_action = last_action;
}

uint32_t wd_asic_window_ms(double expected_ghs, uint8_t diff_power)
{
    if (expected_ghs <= 0.0 || diff_power > 40) {
        return 0;
    }
    double interval_s = ldexp(1.0, diff_power + 32) / (expected_ghs * 1e9);
    double window_ms = interval_s * WD_ASIC_WINDOW_INTERVALS * 1000.0;
    if (window_ms > WD_ASIC_MAX_WINDOW_MS) {
        return 0;
    }
    if (window_ms < WD_ASIC_MIN_WINDOW_MS) {
        return WD_ASIC_MIN_WINDOW_MS;
    }
    return (uint32_t) window_ms;
}

wd_asic_action_t wd_asic_step(wd_asic_t *wd, uint64_t now_ms, const wd_asic_input_t *in)
{
    uint64_t dt = now_ms > wd->last_ms ? now_ms - wd->last_ms : 0;
    if (dt > WD_STEP_CAP_MS) {
        dt = WD_STEP_CAP_MS;
    }
    wd->last_ms = now_ms;

    if (wd->recoveries > 0 && now_ms - wd->last_recovery_ms >= WD_ASIC_BUDGET_RESET_MS) {
        wd->recoveries = 0;
    }

    // Each signal has its own clock, held while there is no work and reset
    // when the chain isn't running
    bool new_results = in->results != wd->last_results;
    if (new_results) {
        wd->last_results = in->results;
        wd->silent_ms = 0;
        wd->results_since_recovery = true;
    }
    bool hashrate_moved = in->hashrate_1m != wd->last_hashrate;
    if (hashrate_moved) {
        wd->last_hashrate = in->hashrate_1m;
        wd->frozen_ms = 0;
        wd->hashrate_since_recovery = true;
    }

    if (!in->running) {
        wd->silent_ms = 0;
        wd->frozen_ms = 0;
        return WD_ASIC_NONE;
    }
    if (!in->has_work) {
        return WD_ASIC_NONE;
    }

    if (in->window_ms != 0 && !new_results) {
        wd->silent_ms += dt;
    }
    if (!hashrate_moved) {
        wd->frozen_ms += dt;
    }

    wd_asic_reason_t reason;
    if (in->window_ms != 0 && wd->silent_ms >= in->window_ms) {
        reason = WD_ASIC_REASON_NO_RESULTS;
    } else if (wd->frozen_ms >= WD_ASIC_FROZEN_MS) {
        reason = WD_ASIC_REASON_FROZEN_HASHRATE;
    } else {
        return WD_ASIC_NONE;
    }
    wd->silent_ms = 0;
    wd->frozen_ms = 0;

    if (wd->recoveries >= WD_ASIC_MAX_RECOVERIES) {
        wd->reason = reason;
        return WD_ASIC_GIVE_UP;
    }
    wd->recoveries++;
    wd->last_recovery_ms = now_ms;

    // Escalate when the signal that caused the last re-init never moved again
    bool helped = wd->reason == WD_ASIC_REASON_FROZEN_HASHRATE ? wd->hashrate_since_recovery
                                                              : wd->results_since_recovery;
    wd_asic_action_t action = (wd->last_action == WD_ASIC_REINIT && !helped) ? WD_ASIC_RESTART : WD_ASIC_REINIT;
    wd->last_action = action;
    wd->reason = reason;
    wd->results_since_recovery = false;
    wd->hashrate_since_recovery = false;
    return action;
}

void wd_pool_init(wd_pool_t *wd)
{
    memset(wd, 0, sizeof(*wd));
}

wd_pool_action_t wd_pool_step(wd_pool_t *wd, uint64_t now_ms, bool active,
                              uint64_t work_received, uint64_t submitted, uint64_t resolved)
{
    if (!active || !wd->active) {
        wd->active = active;
        wd->last_work = work_received;
        wd->work_since_ms = now_ms;
        wd->last_submitted = submitted;
        wd->last_resolved = resolved;
        wd->unanswered_since_ms = 0;
        return WD_POOL_NONE;
    }

    if (work_received != wd->last_work) {
        wd->last_work = work_received;
        wd->work_since_ms = now_ms;
    } else if (now_ms - wd->work_since_ms >= WD_POOL_NO_WORK_MS) {
        wd->work_since_ms = now_ms;
        wd->unanswered_since_ms = 0;
        return WD_POOL_NO_WORK;
    }

    // A reply, or a new session (the counters start over)
    if (resolved != wd->last_resolved || submitted < wd->last_submitted) {
        wd->last_resolved = resolved;
        wd->unanswered_since_ms = 0;
    }
    if (submitted != wd->last_submitted) {
        if (submitted > resolved && wd->unanswered_since_ms == 0) {
            wd->unanswered_since_ms = now_ms;
        }
        wd->last_submitted = submitted;
    }
    if (wd->unanswered_since_ms != 0 && now_ms - wd->unanswered_since_ms >= WD_POOL_NO_REPLY_MS) {
        wd->unanswered_since_ms = 0;
        wd->work_since_ms = now_ms;
        return WD_POOL_NO_REPLIES;
    }
    return WD_POOL_NONE;
}
