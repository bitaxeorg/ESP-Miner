#include "power_settings.h"
#include "global_state.h"
#include "vcore.h"
#include "asic.h"
#include "asic_common.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "power_settings";

static bool set_voltage(GlobalState *state, uint16_t voltage_mv, uint16_t *last_voltage_mv)
{
    if (VCORE_set_voltage(state, (float)voltage_mv / 1000.0f) != ESP_OK) {
        *last_voltage_mv = 0;
        return false;
    }
    *last_voltage_mv = voltage_mv;
    return true;
}

bool POWER_MANAGEMENT_apply_settings(GlobalState *state, uint16_t voltage_mv, float frequency,
                                     uint16_t *last_voltage_mv, float *last_frequency)
{
    // With an uncertain rail after an error, downclock before reapplying voltage.
    bool unknown_downclock = *last_voltage_mv == 0 && frequency < state->POWER_MANAGEMENT_MODULE.actual_frequency;
    if (voltage_mv > *last_voltage_mv && !unknown_downclock &&
        !set_voltage(state, voltage_mv, last_voltage_mv)) {
        return false;
    }

    if (frequency != *last_frequency) {
        ESP_LOGI(TAG, "New ASIC frequency requested: %g MHz (current: %g MHz)", frequency, *last_frequency);
        state->POWER_MANAGEMENT_MODULE.frequency_value = frequency;
        state->POWER_MANAGEMENT_MODULE.expected_hashrate = frequency *
            state->DEVICE_CONFIG.family.asic.small_core_count * state->DEVICE_CONFIG.family.asic_count / 1000.0f;

        bool frequency_set = ASIC_set_frequency(state);
        ASIC_set_nonce_space(state);
        *last_frequency = frequency_set ? frequency : NAN;
        if (!frequency_set) {
            return false;
        }
        ASIC_set_difficulty(state, calculate_effective_asic_difficulty(state->POWER_MANAGEMENT_MODULE.expected_hashrate));
    }

    if (voltage_mv != *last_voltage_mv && !set_voltage(state, voltage_mv, last_voltage_mv)) {
        return false;
    }
    return true;
}
