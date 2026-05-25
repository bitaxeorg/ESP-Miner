#ifndef PREDICTIVE_EFFICIENCY_H_
#define PREDICTIVE_EFFICIENCY_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PREDICTIVE_EFFICIENCY_HOLD = 0,
    PREDICTIVE_EFFICIENCY_COOL_DOWN,
    PREDICTIVE_EFFICIENCY_REDUCE_CLOCK,
    PREDICTIVE_EFFICIENCY_CHECK_POOL,
    PREDICTIVE_EFFICIENCY_EXPLORE_UP,
} PredictiveEfficiencyAction;

typedef enum {
    PREDICTIVE_AUTOTUNE_DISABLED = 0,
    PREDICTIVE_AUTOTUNE_WARMUP,
    PREDICTIVE_AUTOTUNE_MONITOR,
    PREDICTIVE_AUTOTUNE_TRIAL,
    PREDICTIVE_AUTOTUNE_ROLLBACK,
    PREDICTIVE_AUTOTUNE_THROTTLE,
} PredictiveAutotuneState;

typedef struct {
    bool enabled;
    bool autotune_enabled;
    bool bm1368_profile;
    bool bm1370_profile;
    bool gamma_profile;
    char profile_name[16];
    float hash_per_watt;
    float useful_share_ratio;
    float thermal_margin_c;
    float hashrate_ratio;
    float error_penalty;
    float latency_penalty;
    float score;
    float tuned_frequency;
    float best_frequency;
    float trial_frequency;
    float baseline_score;
    PredictiveEfficiencyAction action;
    PredictiveAutotuneState agent_state;
    char reason[96];
    char agent_reason[96];
    uint64_t last_update_ms;
    uint64_t last_tune_ms;
} PredictiveEfficiencyModule;

void predictive_efficiency_update(void *global_state, uint64_t now_ms);
const char *predictive_efficiency_action_name(PredictiveEfficiencyAction action);
const char *predictive_autotune_state_name(PredictiveAutotuneState state);

#endif
