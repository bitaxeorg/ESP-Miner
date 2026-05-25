#include <stdio.h>

#include "global_state.h"
#include "predictive_efficiency.h"

#define BM1370_TARGET_TEMP_C 70.0f
#define BM1370_MAX_TEMP_C 80.0f
#define BM1370_MIN_EXPLORE_MARGIN_C 12.0f
#define MIN_VALID_POWER_W 1.0f
#define MAX_SCORE 100.0f

static float clampf(float value, float min, float max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static float hottest_chip_temp(const PowerManagementModule *power)
{
    float temp = power->chip_temp_avg;
    if (power->chip_temp2_avg > temp) {
        temp = power->chip_temp2_avg;
    }
    return temp;
}

static bool is_bm1370_profile(const GlobalState *global_state)
{
    const DeviceConfig *device = &global_state->DEVICE_CONFIG;
    return device->family.asic.id == BM1370;
}

const char *predictive_efficiency_action_name(PredictiveEfficiencyAction action)
{
    switch (action) {
        case PREDICTIVE_EFFICIENCY_HOLD:
            return "hold";
        case PREDICTIVE_EFFICIENCY_COOL_DOWN:
            return "cool_down";
        case PREDICTIVE_EFFICIENCY_REDUCE_CLOCK:
            return "reduce_clock";
        case PREDICTIVE_EFFICIENCY_CHECK_POOL:
            return "check_pool";
        case PREDICTIVE_EFFICIENCY_EXPLORE_UP:
            return "explore_up";
        default:
            return "unknown";
    }
}

void predictive_efficiency_update(void *pvParameters, uint64_t now_ms)
{
    GlobalState *global_state = (GlobalState *)pvParameters;
    PredictiveEfficiencyModule *module = &global_state->PREDICTIVE_EFFICIENCY_MODULE;
    const PowerManagementModule *power = &global_state->POWER_MANAGEMENT_MODULE;
    const SystemModule *system = &global_state->SYSTEM_MODULE;

    module->enabled = true;
    module->bm1370_profile = is_bm1370_profile(global_state);
    module->gamma_profile = module->bm1370_profile;
    module->last_update_ms = now_ms;

    float accepted = (float)system->shares_accepted;
    float rejected = (float)system->shares_rejected;
    float total_shares = accepted + rejected;
    module->useful_share_ratio = total_shares > 0.0f ? accepted / total_shares : 1.0f;

    module->hash_per_watt = power->power > MIN_VALID_POWER_W
        ? system->hashrate_1m / power->power
        : 0.0f;

    module->hashrate_ratio = power->expected_hashrate > 0.0f
        ? system->hashrate_1m / power->expected_hashrate
        : 0.0f;

    float temp = hottest_chip_temp(power);
    module->thermal_margin_c = BM1370_MAX_TEMP_C - temp;
    module->error_penalty = clampf(system->error_percentage / 5.0f, 0.0f, 1.0f);
    module->latency_penalty = clampf(system->response_time / 2500.0f, 0.0f, 1.0f);

    float thermal_score = clampf(module->thermal_margin_c / (BM1370_MAX_TEMP_C - BM1370_TARGET_TEMP_C), 0.0f, 1.0f);
    float hashrate_score = clampf(module->hashrate_ratio, 0.0f, 1.15f) / 1.15f;
    float share_score = clampf(module->useful_share_ratio, 0.0f, 1.0f);
    float stability_score = 1.0f - clampf((module->error_penalty * 0.7f) + (module->latency_penalty * 0.3f), 0.0f, 1.0f);

    module->score = MAX_SCORE * (
        hashrate_score * 0.35f
        + share_score * 0.25f
        + thermal_score * 0.20f
        + stability_score * 0.20f
    );

    if (!module->bm1370_profile) {
        module->action = PREDICTIVE_EFFICIENCY_HOLD;
        snprintf(module->reason, sizeof(module->reason), "profile is passive outside BM1370 ASIC devices");
    } else if (temp >= BM1370_MAX_TEMP_C || power->vr_temp >= 105.0f) {
        module->action = PREDICTIVE_EFFICIENCY_COOL_DOWN;
        snprintf(module->reason, sizeof(module->reason), "thermal headroom is exhausted");
    } else if (system->error_percentage > 5.0f || module->hashrate_ratio < 0.82f) {
        module->action = PREDICTIVE_EFFICIENCY_REDUCE_CLOCK;
        snprintf(module->reason, sizeof(module->reason), "hashrate or ASIC error trend is unstable");
    } else if (total_shares >= 8.0f && module->useful_share_ratio < 0.90f) {
        module->action = PREDICTIVE_EFFICIENCY_CHECK_POOL;
        snprintf(module->reason, sizeof(module->reason), "share rejection ratio is above target");
    } else if (module->thermal_margin_c > BM1370_MIN_EXPLORE_MARGIN_C && system->error_percentage < 1.0f && module->hashrate_ratio > 0.94f) {
        module->action = PREDICTIVE_EFFICIENCY_EXPLORE_UP;
        snprintf(module->reason, sizeof(module->reason), "BM1370 has thermal and error margin for a small upward trial");
    } else {
        module->action = PREDICTIVE_EFFICIENCY_HOLD;
        snprintf(module->reason, sizeof(module->reason), "BM1370 efficiency is inside the current stability band");
    }
}
