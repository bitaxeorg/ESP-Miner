/**
 * @file power_management_calc.c
 * @brief Pure calculation functions for power management
 *
 * Implementation of hardware-independent calculation functions.
 * These can be unit tested without ESP32 hardware.
 */

#include "power_management_calc.h"

/* ============================================
 * Utility Functions
 * ============================================ */

float pm_clamp_f(float value, float min, float max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

uint16_t pm_clamp_u16(uint16_t value, uint16_t min, uint16_t max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

float pm_calc_efficiency(float power, float hashrate)
{
    if (hashrate <= 0.0f) {
        return 0.0f;
    }
    /* Convert GH/s to TH/s (divide by 1000) for J/TH result */
    return power / (hashrate / 1000.0f);
}

/* ============================================
 * Fan Speed Calculation
 * ============================================ */

float pm_calc_fan_speed_percent_ex(float chip_temp, float min_temp,
                                    float throttle_temp, float min_fan_speed)
{
    if (chip_temp < min_temp) {
        return min_fan_speed;
    }

    if (chip_temp >= throttle_temp) {
        return 100.0f;
    }

    /* Linear interpolation between min_temp and throttle_temp */
    float temp_range = throttle_temp - min_temp;
    float fan_range = 100.0f - min_fan_speed;

    return ((chip_temp - min_temp) / temp_range) * fan_range + min_fan_speed;
}

float pm_calc_fan_speed_percent(float chip_temp)
{
    return pm_calc_fan_speed_percent_ex(chip_temp,
                                        PM_MIN_FAN_TEMP,
                                        PM_THROTTLE_TEMP,
                                        PM_MIN_FAN_SPEED);
}

/* ============================================
 * Overheat Detection
 * ============================================ */

pm_overheat_type_t pm_check_overheat(float chip_temp, float vr_temp,
                                      float chip_threshold, float vr_threshold)
{
    bool chip_hot = chip_temp > chip_threshold;
    bool vr_hot = (vr_temp > 0.0f) && (vr_temp > vr_threshold);

    if (chip_hot && vr_hot) {
        return PM_OVERHEAT_BOTH;
    } else if (chip_hot) {
        return PM_OVERHEAT_CHIP;
    } else if (vr_hot) {
        return PM_OVERHEAT_VR;
    }

    return PM_OVERHEAT_NONE;
}

bool pm_should_trigger_overheat(float chip_temp, float vr_temp,
                                 uint16_t frequency, uint16_t voltage)
{
    /* Don't trigger if already at safe values */
    if (frequency <= 50 && voltage <= 1000) {
        return false;
    }

    pm_overheat_type_t overheat = pm_check_overheat(chip_temp, vr_temp,
                                                     PM_THROTTLE_TEMP,
                                                     PM_TPS546_THROTTLE_TEMP);

    return overheat != PM_OVERHEAT_NONE;
}

pm_overheat_severity_t pm_calc_overheat_severity(uint16_t overheat_count)
{
    /* Every 3rd event triggers hard recovery (counts 3, 6, 9, etc.)
     * Note: overheat_count is the count BEFORE increment */
    if (((overheat_count + 1) % 3) == 0) {
        return PM_SEVERITY_HARD;
    }

    return PM_SEVERITY_SOFT;
}

/* ============================================
 * Autotune Calculations
 * ============================================ */

bool pm_is_hashrate_low(float current_hashrate, float target_hashrate,
                        float threshold_percent)
{
    if (target_hashrate <= 0.0f) {
        return false;
    }

    float threshold = target_hashrate * (threshold_percent / 100.0f);
    return current_hashrate < threshold;
}

float pm_calc_target_hashrate(uint16_t frequency, uint16_t small_core_count,
                               uint16_t asic_count)
{
    return (float)frequency * ((float)(small_core_count * asic_count) / 1000.0f);
}

uint32_t pm_get_autotune_interval_ms(float chip_temp)
{
    if (chip_temp < 68.0f) {
        return 300000; /* 5 minutes for normal operation */
    }
    return 500; /* 500ms for elevated temperatures */
}

pm_autotune_decision_t pm_calc_autotune(const pm_autotune_input_t* input,
                                         const pm_autotune_limits_t* limits,
                                         uint8_t target_temp,
                                         uint8_t consecutive_low_hashrate,
                                         uint32_t ms_since_last_adjust)
{
    pm_autotune_decision_t decision = {0};

    /* Validate inputs */
    if (!input || !limits) {
        decision.skip_reason_invalid = true;
        return decision;
    }

    /* Check for invalid temperature reading */
    if ((uint8_t)input->chip_temp == 255) {
        decision.skip_reason_invalid = true;
        return decision;
    }

    /* Check for zero hashrate */
    if (input->current_hashrate <= 0.0f) {
        decision.skip_reason_invalid = true;
        return decision;
    }

    /* Check warmup period */
    if (input->uptime_seconds < PM_AUTOTUNE_WARMUP_SECONDS &&
        input->chip_temp < (float)target_temp) {
        decision.skip_reason_warmup = true;
        return decision;
    }

    /* Check timing interval */
    uint32_t required_interval = pm_get_autotune_interval_ms(input->chip_temp);
    if (ms_since_last_adjust < required_interval) {
        decision.skip_reason_timing = true;
        return decision;
    }

    /* Check for consecutive low hashrate - trigger preset reset */
    if (consecutive_low_hashrate >= PM_MAX_LOW_HASHRATE_ATTEMPTS) {
        decision.should_reset_preset = true;
        return decision;
    }

    /* Temperature-based adjustments */
    int8_t temp_diff = (int8_t)input->chip_temp - (int8_t)target_temp;

    /* Temperature within target range (-2 to +2) */
    if (temp_diff >= -2 && temp_diff <= 2) {
        /* Check hashrate - if below 80% of target, increase voltage */
        float hashrate_diff_percent = ((input->current_hashrate - input->target_hashrate)
                                       / input->target_hashrate) * 100.0f;

        if (hashrate_diff_percent < -20.0f) {
            /* Increase voltage by 10mV */
            uint16_t new_voltage = input->current_voltage + 10;
            if (new_voltage <= limits->max_voltage) {
                decision.should_adjust = true;
                decision.new_voltage = new_voltage;
            }
        }
        /* Else: at target, no adjustment needed */
        return decision;
    }

    /* Temperature below target - can increase performance */
    if (temp_diff < -2) {
        bool can_adjust = false;

        /* Try to increase frequency by 2% if within limits */
        if (input->current_frequency < limits->max_frequency &&
            input->current_power < limits->max_power) {
            uint16_t new_freq = (uint16_t)(input->current_frequency * 1.02f);
            new_freq = pm_clamp_u16(new_freq, limits->min_frequency, limits->max_frequency);

            if (new_freq != input->current_frequency) {
                decision.new_frequency = new_freq;
                can_adjust = true;
            }
        }

        /* Try to increase voltage by 0.2% if within limits */
        if (input->current_voltage < limits->max_voltage &&
            input->current_power < limits->max_power) {
            uint16_t new_volt = (uint16_t)(input->current_voltage * 1.002f);
            new_volt = pm_clamp_u16(new_volt, limits->min_voltage, limits->max_voltage);

            if (new_volt != input->current_voltage) {
                decision.new_voltage = new_volt;
                can_adjust = true;
            }
        }

        decision.should_adjust = can_adjust;
        return decision;
    }

    /* Temperature above target - need to reduce performance */
    {
        bool can_adjust = false;

        /* Decrease frequency by 2% */
        uint16_t new_freq = (uint16_t)(input->current_frequency * 0.98f);
        new_freq = pm_clamp_u16(new_freq, limits->min_frequency, limits->max_frequency);

        if (new_freq != input->current_frequency) {
            decision.new_frequency = new_freq;
            can_adjust = true;
        }

        /* Decrease voltage by 0.2% */
        uint16_t new_volt = (uint16_t)(input->current_voltage * 0.998f);
        new_volt = pm_clamp_u16(new_volt, limits->min_voltage, limits->max_voltage);

        if (new_volt != input->current_voltage) {
            decision.new_voltage = new_volt;
            can_adjust = true;
        }

        decision.should_adjust = can_adjust;
    }

    return decision;
}
