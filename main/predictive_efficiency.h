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

typedef struct {
    bool enabled;
    bool gamma_profile;
    float hash_per_watt;
    float useful_share_ratio;
    float thermal_margin_c;
    float hashrate_ratio;
    float error_penalty;
    float latency_penalty;
    float score;
    PredictiveEfficiencyAction action;
    char reason[96];
    uint64_t last_update_ms;
} PredictiveEfficiencyModule;

void predictive_efficiency_update(void *global_state, uint64_t now_ms);
const char *predictive_efficiency_action_name(PredictiveEfficiencyAction action);

#endif
