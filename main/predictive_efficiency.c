#include <stdio.h>

#include "esp_log.h"
#include "global_state.h"
#include "nvs_config.h"
#include "predictive_efficiency.h"

#define BM1370_TARGET_TEMP_C 70.0f
#define BM1370_MAX_TEMP_C 80.0f
#define BM1370_MIN_EXPLORE_MARGIN_C 12.0f
#define BM1370_AUTOTUNE_THROTTLE_TEMP_C 77.0f
#define BM1370_AUTOTUNE_VR_THROTTLE_TEMP_C 102.0f
#define AUTOTUNE_WARMUP_MS 180000ULL
#define AUTOTUNE_INTERVAL_MS 180000ULL
#define AUTOTUNE_RECOVERY_MS 300000ULL
#define AUTOTUNE_FREQ_STEP_MHZ 25.0f
#define AUTOTUNE_SCORE_DROP_ROLLBACK 4.0f
#define AUTOTUNE_MIN_SCORE_TO_EXPLORE 70.0f
#define AUTOTUNE_MIN_FREQ_MHZ 300.0f
#define MIN_VALID_POWER_W 1.0f
#define MAX_SCORE 100.0f

static const char *TAG = "predictive_efficiency";

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

const char *predictive_autotune_state_name(PredictiveAutotuneState state)
{
    switch (state) {
        case PREDICTIVE_AUTOTUNE_DISABLED:
            return "disabled";
        case PREDICTIVE_AUTOTUNE_WARMUP:
            return "warmup";
        case PREDICTIVE_AUTOTUNE_MONITOR:
            return "monitor";
        case PREDICTIVE_AUTOTUNE_TRIAL:
            return "trial";
        case PREDICTIVE_AUTOTUNE_ROLLBACK:
            return "rollback";
        case PREDICTIVE_AUTOTUNE_THROTTLE:
            return "throttle";
        default:
            return "unknown";
    }
}

static void bm1370_frequency_limits(const GlobalState *global_state, float *min_frequency, float *max_frequency)
{
    const uint16_t *options = global_state->DEVICE_CONFIG.family.asic.frequency_options;

    *min_frequency = AUTOTUNE_MIN_FREQ_MHZ;
    *max_frequency = global_state->DEVICE_CONFIG.family.asic.default_frequency_mhz;

    if (!options) return;

    bool found = false;
    for (int i = 0; options[i] != 0; i++) {
        float option = (float)options[i];
        if (!found || option < *min_frequency) *min_frequency = option;
        if (!found || option > *max_frequency) *max_frequency = option;
        found = true;
    }
}

static float bounded_frequency(const GlobalState *global_state, float requested)
{
    float min_frequency;
    float max_frequency;
    bm1370_frequency_limits(global_state, &min_frequency, &max_frequency);
    return clampf(requested, min_frequency, max_frequency);
}

static void set_agent_state(PredictiveEfficiencyModule *module, PredictiveAutotuneState state, const char *reason)
{
    module->agent_state = state;
    snprintf(module->agent_reason, sizeof(module->agent_reason), "%s", reason);
}

static void predictive_autotune_update(GlobalState *global_state, uint64_t now_ms, float hottest_temp)
{
    PredictiveEfficiencyModule *module = &global_state->PREDICTIVE_EFFICIENCY_MODULE;
    const SystemModule *system = &global_state->SYSTEM_MODULE;
    const PowerManagementModule *power = &global_state->POWER_MANAGEMENT_MODULE;

    module->autotune_enabled = nvs_config_get_bool(NVS_CONFIG_PREDICTIVE_AUTOTUNE);
    module->tuned_frequency = nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY);

    if (!module->autotune_enabled) {
        set_agent_state(module, PREDICTIVE_AUTOTUNE_DISABLED, "predictive autotune is disabled");
        return;
    }

    if (!module->bm1370_profile) {
        set_agent_state(module, PREDICTIVE_AUTOTUNE_DISABLED, "autotune only runs on BM1370 ASIC profiles");
        return;
    }

    uint64_t uptime_ms = now_ms - (uint64_t)(system->start_time / 1000);
    if (uptime_ms < AUTOTUNE_WARMUP_MS || system->hashrate_1m <= 0.0f) {
        module->best_frequency = module->tuned_frequency;
        module->baseline_score = module->score;
        module->last_tune_ms = now_ms;
        set_agent_state(module, PREDICTIVE_AUTOTUNE_WARMUP, "waiting for stable BM1370 telemetry before tuning");
        return;
    }

    if (module->best_frequency <= 0.0f) {
        module->best_frequency = module->tuned_frequency;
        module->baseline_score = module->score;
    }

    if (hottest_temp >= BM1370_AUTOTUNE_THROTTLE_TEMP_C || power->vr_temp >= BM1370_AUTOTUNE_VR_THROTTLE_TEMP_C) {
        float target = bounded_frequency(global_state, module->tuned_frequency - AUTOTUNE_FREQ_STEP_MHZ);
        if (target < module->tuned_frequency) {
            nvs_config_set_float(NVS_CONFIG_ASIC_FREQUENCY, target);
            module->trial_frequency = target;
            module->last_tune_ms = now_ms;
            ESP_LOGW(TAG, "BM1370 autotune thermal throttle: %.0f -> %.0f MHz", module->tuned_frequency, target);
        }
        module->best_frequency = target;
        module->baseline_score = module->score;
        set_agent_state(module, PREDICTIVE_AUTOTUNE_THROTTLE, "thermal limit reached, stepping frequency down");
        return;
    }

    if (module->agent_state == PREDICTIVE_AUTOTUNE_TRIAL) {
        if (module->score + AUTOTUNE_SCORE_DROP_ROLLBACK < module->baseline_score
            || system->error_percentage > 5.0f
            || module->useful_share_ratio < 0.90f) {
            float target = bounded_frequency(global_state, module->best_frequency);
            nvs_config_set_float(NVS_CONFIG_ASIC_FREQUENCY, target);
            module->trial_frequency = target;
            module->last_tune_ms = now_ms;
            module->baseline_score = module->score;
            set_agent_state(module, PREDICTIVE_AUTOTUNE_ROLLBACK, "trial worsened efficiency or stability, rolling back");
            ESP_LOGW(TAG, "BM1370 autotune rollback to %.0f MHz", target);
            return;
        }

        if (module->score >= module->baseline_score) {
            module->best_frequency = module->tuned_frequency;
            module->baseline_score = module->score;
            set_agent_state(module, PREDICTIVE_AUTOTUNE_MONITOR, "trial accepted as current best BM1370 frequency");
        }
    }

    uint64_t interval = module->agent_state == PREDICTIVE_AUTOTUNE_ROLLBACK
        ? AUTOTUNE_RECOVERY_MS
        : AUTOTUNE_INTERVAL_MS;
    if (now_ms - module->last_tune_ms < interval) {
        set_agent_state(module, PREDICTIVE_AUTOTUNE_MONITOR, "monitoring BM1370 efficiency window");
        return;
    }

    if (module->action == PREDICTIVE_EFFICIENCY_REDUCE_CLOCK || system->error_percentage > 3.0f || module->score < 55.0f) {
        float target = bounded_frequency(global_state, module->tuned_frequency - AUTOTUNE_FREQ_STEP_MHZ);
        if (target < module->tuned_frequency) {
            nvs_config_set_float(NVS_CONFIG_ASIC_FREQUENCY, target);
            module->trial_frequency = target;
            module->best_frequency = target;
            module->baseline_score = module->score;
            module->last_tune_ms = now_ms;
            set_agent_state(module, PREDICTIVE_AUTOTUNE_THROTTLE, "instability detected, stepping BM1370 frequency down");
            ESP_LOGW(TAG, "BM1370 autotune reduce clock: %.0f -> %.0f MHz", module->tuned_frequency, target);
            return;
        }
    }

    if (module->action == PREDICTIVE_EFFICIENCY_EXPLORE_UP && module->score >= AUTOTUNE_MIN_SCORE_TO_EXPLORE) {
        float target = bounded_frequency(global_state, module->tuned_frequency + AUTOTUNE_FREQ_STEP_MHZ);
        if (target > module->tuned_frequency) {
            module->best_frequency = module->tuned_frequency;
            module->baseline_score = module->score;
            module->trial_frequency = target;
            module->last_tune_ms = now_ms;
            nvs_config_set_float(NVS_CONFIG_ASIC_FREQUENCY, target);
            set_agent_state(module, PREDICTIVE_AUTOTUNE_TRIAL, "testing a small BM1370 frequency increase");
            ESP_LOGI(TAG, "BM1370 autotune trial: %.0f -> %.0f MHz", module->tuned_frequency, target);
            return;
        }
    }

    set_agent_state(module, PREDICTIVE_AUTOTUNE_MONITOR, "BM1370 agent is holding current frequency");
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

    predictive_autotune_update(global_state, now_ms, temp);
}
