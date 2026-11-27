#include "mining_watchdog.h"

#include <math.h>
#include <string.h>

void wd_asic_init(wd_asic_t *wd, uint8_t recoveries, wd_asic_action_t last_action)
{
    memset(wd, 0, sizeof(*wd));
    wd->recoveries = recoveries;
    wd->last_action = last_action;
}

uint32_t wd_asic_window_s(double expected_ghs, uint8_t diff_power)
{
    if (expected_ghs <= 0.0 || diff_power > 40) {
        return 0;
    }
    double interval_s = ldexp(1.0, diff_power + 32) / (expected_ghs * 1e9);
    double window_s = interval_s * WD_ASIC_WINDOW_INTERVALS;
    if (window_s > WD_ASIC_MAX_WINDOW_S) {
        return 0;
    }
    if (window_s < WD_ASIC_MIN_WINDOW_S) {
        return WD_ASIC_MIN_WINDOW_S;
    }
    return (uint32_t) window_s;
}

wd_asic_action_t wd_asic_step(wd_asic_t *wd, const wd_asic_input_t *in)
{
    if (wd->since_recovery_s < WD_ASIC_BUDGET_S) {
        wd->since_recovery_s++;
    }
    if (wd->recoveries > 0 && wd->since_recovery_s >= WD_ASIC_BUDGET_S) {
        wd->recoveries = 0;
    }

    // Each signal has its own count, held while there is no work and reset
    // when the chain isn't running
    bool new_results = in->results != wd->last_results;
    if (new_results) {
        wd->last_results = in->results;
        wd->silent_s = 0;
        wd->results_since_recovery = true;
    }
    bool hashrate_moved = in->hashrate_1m != wd->last_hashrate;
    if (hashrate_moved) {
        wd->last_hashrate = in->hashrate_1m;
        wd->frozen_s = 0;
        wd->hashrate_since_recovery = true;
    }

    if (!in->running) {
        wd->silent_s = 0;
        wd->frozen_s = 0;
        return WD_ASIC_NONE;
    }
    if (!in->has_work) {
        return WD_ASIC_NONE;
    }

    if (in->window_s != 0 && !new_results) {
        wd->silent_s++;
    }
    if (!hashrate_moved) {
        wd->frozen_s++;
    }

    wd_asic_reason_t reason;
    if (in->window_s != 0 && wd->silent_s >= in->window_s) {
        reason = WD_ASIC_REASON_NO_RESULTS;
    } else if (wd->frozen_s >= WD_ASIC_FROZEN_S) {
        reason = WD_ASIC_REASON_FROZEN_HASHRATE;
    } else {
        return WD_ASIC_NONE;
    }
    wd->silent_s = 0;
    wd->frozen_s = 0;

    if (wd->recoveries >= WD_ASIC_MAX_RECOVERIES) {
        wd->reason = reason;
        return WD_ASIC_GIVE_UP;
    }
    wd->recoveries++;
    wd->since_recovery_s = 0;

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

wd_pool_action_t wd_pool_step(wd_pool_t *wd, bool active, uint64_t work_received, uint64_t submitted,
                              uint64_t resolved)
{
    if (!active || !wd->active) {
        wd_pool_init(wd);
        wd->active = active;
        wd->last_work = work_received;
        wd->last_submitted = submitted;
        wd->last_resolved = resolved;
        return WD_POOL_NONE;
    }

    if (work_received != wd->last_work) {
        wd->last_work = work_received;
        wd->no_work_s = 0;
    } else if (++wd->no_work_s >= WD_POOL_NO_WORK_S) {
        wd->no_work_s = 0;
        wd->waiting = false;
        return WD_POOL_NO_WORK;
    }

    // A reply, or a new session (the counters start over)
    if (resolved != wd->last_resolved || submitted < wd->last_submitted) {
        wd->last_resolved = resolved;
        wd->waiting = false;
    }
    bool started = false;
    if (submitted != wd->last_submitted) {
        if (submitted > resolved && !wd->waiting) {
            wd->waiting = true;
            wd->waiting_s = 0;
            started = true;
        }
        wd->last_submitted = submitted;
    }
    if (wd->waiting && !started && ++wd->waiting_s >= WD_POOL_NO_REPLY_S) {
        wd->waiting = false;
        wd->no_work_s = 0;
        return WD_POOL_NO_REPLIES;
    }
    return WD_POOL_NONE;
}
