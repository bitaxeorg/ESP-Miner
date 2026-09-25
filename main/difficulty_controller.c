#include "difficulty_controller.h"

#include <esp_log.h>
#include <esp_timer.h>

#include "asic.h"
#include "asic_common.h"
#include "device_config.h"
#include "global_state.h"

#define DIFFICULTY_INCREASE_GRACE_PERIOD_S 30.0
#define DIFFICULTY_INCREASE_GRACE_PERIOD_US ((uint64_t)(DIFFICULTY_INCREASE_GRACE_PERIOD_S * 1000000ULL))

static const char *TAG = "difficulty_controller";

static double s_pending_difficulty = 0.0;
static int64_t s_pending_increase_time_us = 0;

void difficulty_controller_update(GlobalState * GLOBAL_STATE)
{
    if (!GLOBAL_STATE || !GLOBAL_STATE->ASIC_initalized) {
        return;
    }

    float expected_ghs = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate;
    if (expected_ghs <= 0.0f) {
        float freq = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value;
        if (freq <= 0.0f) {
            freq = (float)GLOBAL_STATE->DEVICE_CONFIG.family.asic.default_frequency_mhz;
        }
        expected_ghs = freq * GLOBAL_STATE->DEVICE_CONFIG.family.asic.small_core_count * GLOBAL_STATE->DEVICE_CONFIG.family.asic_count / 1000.0f;
    }

    double interval_s = (GLOBAL_STATE->target_share_interval_s > 0.0)
                            ? GLOBAL_STATE->target_share_interval_s
                            : DEFAULT_SHARE_INTERVAL_S;

    double eff_diff = calculate_effective_asic_difficulty((double)expected_ghs, interval_s, GLOBAL_STATE->SYSTEM_MODULE.pool_difficulty);
    double diff_to_apply = 0.0;

    // Initial setup, self-test, or downward adjustment: apply immediately!
    if (GLOBAL_STATE->current_difficulty <= 0.0 || GLOBAL_STATE->SELF_TEST_MODULE.is_active || eff_diff < GLOBAL_STATE->current_difficulty) {
        GLOBAL_STATE->current_difficulty = eff_diff;
        s_pending_difficulty = 0.0;
        s_pending_increase_time_us = 0;
        ESP_LOGI(TAG, "ASIC difficulty updated: pool %.2f -> effective %.0f",
                 GLOBAL_STATE->SYSTEM_MODULE.pool_difficulty, eff_diff);
        diff_to_apply = eff_diff;
    } else if (eff_diff == GLOBAL_STATE->current_difficulty && s_pending_difficulty <= 0.0) {
        return;
    } else {
        // Upward adjustment (eff_diff > GLOBAL_STATE->current_difficulty):
        int64_t now_us = esp_timer_get_time();

        // Start grace period timer if this is a new increase
        if (s_pending_difficulty != eff_diff) {
            s_pending_difficulty = eff_diff;
            s_pending_increase_time_us = now_us;
            ESP_LOGI(TAG, "ASIC difficulty increase to %.0f scheduled (grace period %.0fs)",
                     eff_diff, (double)DIFFICULTY_INCREASE_GRACE_PERIOD_S);
            return;
        }

        // Check if grace period has elapsed
        if ((now_us - s_pending_increase_time_us) >= DIFFICULTY_INCREASE_GRACE_PERIOD_US) {
            GLOBAL_STATE->current_difficulty = s_pending_difficulty;
            s_pending_difficulty = 0.0;
            s_pending_increase_time_us = 0;
            ESP_LOGI(TAG, "Grace period elapsed; applying ASIC difficulty increase: %.0f", GLOBAL_STATE->current_difficulty);
            diff_to_apply = GLOBAL_STATE->current_difficulty;
        } else {
            return;
        }
    }

    if (diff_to_apply > 0.0) {
        ASIC_set_difficulty(GLOBAL_STATE, diff_to_apply);
    }
}
