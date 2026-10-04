#include "bzm/telemetry_internal.h"

#include <math.h>

static bool valid_asic_id(uint8_t asic_id)
{
    return bzm_topology_asic_index(asic_id, NULL);
}

float bzm_telemetry_temperature_from_code(uint16_t code)
{
    return -293.8f + (631.8f * (((float) (code & 0x0fffU)) - 0.5f) / 4096.0f);
}

float bzm_telemetry_voltage_from_code_mv(uint16_t code)
{
    /* Intel SDK 14-bit VM conversion; deliberately not display-clamped. */
    return 1000.0f * 0.4f * 0.7067f * ((6.0f * (float) (code & 0x3fffU) / 16384.0f) - (3.0f / 16384.0f) - 1.0f);
}

bool bzm_telemetry_decode(uint8_t asic_id, const uint8_t * payload, size_t payload_length, uint64_t timestamp_us,
                          bzm_telemetry_sample_t * sample)
{
    if (!valid_asic_id(asic_id) || payload == NULL || payload_length != BZM_TDM_TELEMETRY_PAYLOAD_SIZE || sample == NULL) {
        return false;
    }

    uint8_t thermal = payload[0];
    uint8_t voltage = payload[2];
    uint8_t clocks = payload[7];
    *sample = (bzm_telemetry_sample_t){
        .asic_id = asic_id,
        .timestamp_us = timestamp_us,
        .received = true,
        .temperature_code = (uint16_t) ((thermal & 0x0fU) << 8) | payload[1],
        .thermal_enabled = (thermal & 0x80U) != 0,
        .thermal_validity = (thermal & 0x40U) != 0,
        .thermal_fault = (thermal & 0x20U) != 0,
        .thermal_trip = (thermal & 0x10U) != 0,
        .ch0_code = (uint16_t) ((voltage & 0x3fU) << 8) | payload[3],
        .ch1_code = (uint16_t) (payload[4] << 6) | (payload[5] & 0x3fU),
        .ch2_code = (uint16_t) ((clocks & 0x0fU) << 10) | (uint16_t) (payload[6] << 2) | (uint16_t) (payload[5] >> 6),
        .voltage_enabled = (voltage & 0x80U) != 0,
        .voltage_trip = (voltage & 0x40U) != 0,
        .voltage_fault = (clocks & 0x10U) != 0,
        .pll_locked = (clocks & 0x80U) != 0,
    };
    sample->temperature_c = bzm_telemetry_temperature_from_code(sample->temperature_code);
    sample->ch0_mv = bzm_telemetry_voltage_from_code_mv(sample->ch0_code);
    sample->ch1_mv = bzm_telemetry_voltage_from_code_mv(sample->ch1_code);
    sample->ch2_mv = bzm_telemetry_voltage_from_code_mv(sample->ch2_code);
    sample->thermal_valid = sample->thermal_enabled && sample->thermal_validity && !sample->thermal_fault;
    sample->voltage_valid = sample->voltage_enabled && !sample->voltage_fault;
    sample->valid = sample->thermal_valid && sample->voltage_valid;
    sample->trip = sample->thermal_trip || sample->voltage_trip;
    return true;
}

bool bzm_telemetry_max_temperature(const bzm_telemetry_store_t *store,
                                   uint64_t now_us, uint64_t max_age_us,
                                   float *max_temperature_c)
{
    if (store == NULL || max_temperature_c == NULL) return false;

    float hottest_c = -INFINITY;
    for (size_t index = 0; index < BZM_MAX_ASIC_COUNT; ++index) {
        const bzm_telemetry_sample_t *sample = &store->samples[index];
        if (sample->asic_id != bzm_asic_wire_ids[index] ||
            !sample->thermal_valid || !isfinite(sample->temperature_c) ||
            !bzm_telemetry_sample_is_fresh(
                sample, now_us, max_age_us)) {
            return false;
        }
        if (sample->temperature_c > hottest_c) {
            hottest_c = sample->temperature_c;
        }
    }

    *max_temperature_c = hottest_c;
    return isfinite(*max_temperature_c);
}

void bzm_telemetry_store_init(bzm_telemetry_store_t * store)
{
    if (store == NULL)
        return;
    *store = (bzm_telemetry_store_t){0};
    for (size_t i = 0; i < BZM_MAX_ASIC_COUNT; ++i) {
        store->samples[i].asic_id = bzm_asic_wire_ids[i];
    }
}

bool bzm_telemetry_store_apply_frame(bzm_telemetry_store_t * store, const bzm_frame_t * frame)
{
    if (store == NULL || frame == NULL || frame->type != BZM_FRAME_TELEMETRY || !valid_asic_id(frame->asic_id)) {
        return false;
    }

    size_t index = 0;
    if (!bzm_topology_asic_index(frame->asic_id, &index)) {
        return false;
    }
    return bzm_telemetry_decode(frame->asic_id, frame->payload, frame->payload_length, frame->timestamp_us, &store->samples[index]);
}

const bzm_telemetry_sample_t * bzm_telemetry_store_get(const bzm_telemetry_store_t * store, uint8_t asic_id)
{
    size_t index = 0;
    if (store == NULL || !bzm_topology_asic_index(asic_id, &index))
        return NULL;
    return &store->samples[index];
}

bool bzm_telemetry_value_in_bounds(float value, float minimum, float maximum)
{
    return isfinite(value) && isfinite(minimum) && isfinite(maximum) && minimum <= maximum && value >= minimum && value <= maximum;
}

bool bzm_telemetry_sample_is_fresh(const bzm_telemetry_sample_t * sample, uint64_t now_us, uint64_t max_age_us)
{
    return sample != NULL && sample->received && now_us >= sample->timestamp_us && now_us - sample->timestamp_us <= max_age_us;
}

bool bzm_telemetry_sample_has_immediate_trip(const bzm_telemetry_sample_t * sample)
{
    /* The ASIC trip indication is actionable immediately. Other fields share
     * an unchecksummed TDM status frame with the voltage conversion and are
     * eligible for bounded fresh-sample confirmation while activation is
     * frozen. Direct configuration readback and the independent bridge trip
     * input remain separate hard gates. */
    return sample != NULL && sample->received && sample->trip;
}

static bool bounds_are_valid(const bzm_telemetry_bounds_t * bounds)
{
    return bounds != NULL && isfinite(bounds->temperature_min_c) && isfinite(bounds->temperature_max_c) &&
           bounds->temperature_min_c <= bounds->temperature_max_c && isfinite(bounds->ch0_min_mv) && isfinite(bounds->ch0_max_mv) &&
           bounds->ch0_min_mv <= bounds->ch0_max_mv && isfinite(bounds->ch1_min_mv) && isfinite(bounds->ch1_max_mv) &&
           bounds->ch1_min_mv <= bounds->ch1_max_mv && isfinite(bounds->ch2_abs_max_mv) && bounds->ch2_abs_max_mv >= 0.0f &&
           isfinite(bounds->max_stack_spread_mv) && bounds->max_stack_spread_mv >= 0.0f;
}

static bool sample_is_within_non_ch2_bounds(const bzm_telemetry_sample_t * sample, const bzm_telemetry_bounds_t * bounds)
{
    if (sample == NULL || !bounds_are_valid(bounds) || !isfinite(sample->ch2_mv))
        return false;

    return bzm_telemetry_value_in_bounds(sample->temperature_c, bounds->temperature_min_c, bounds->temperature_max_c) &&
           bzm_telemetry_value_in_bounds(sample->ch0_mv, bounds->ch0_min_mv, bounds->ch0_max_mv) &&
           bzm_telemetry_value_in_bounds(sample->ch1_mv, bounds->ch1_min_mv, bounds->ch1_max_mv) &&
           fabsf(sample->ch0_mv - sample->ch1_mv) <= bounds->max_stack_spread_mv;
}

bool bzm_telemetry_sample_is_safe_except_ch2(const bzm_telemetry_sample_t * sample, uint64_t now_us, uint64_t max_age_us,
                                             const bzm_telemetry_bounds_t * bounds, bool require_clock_locks)
{
    if (sample == NULL || sample->trip || !sample->thermal_valid || !sample->voltage_enabled ||
        !bzm_telemetry_sample_is_fresh(sample, now_us, max_age_us) || !sample_is_within_non_ch2_bounds(sample, bounds) ||
        (require_clock_locks && !sample->pll_locked)) {
        return false;
    }

    if (sample->voltage_valid) {
        return true;
    }

    /* The voltage-fault bit and CH2 high bits share the final byte of an
     * unchecksummed TDM frame. Qualify that bit only when the same frame also
     * contains a finite CH2 excursion, so the existing consecutive-fresh-
     * sample confirmation can distinguish a corrupted frame from a persistent
     * ASIC voltage fault. A voltage fault with an in-range CH2 value remains
     * an immediate failure. Voltage/thermal trip bits are rejected above. */
    return sample->voltage_fault && isfinite(sample->ch2_mv) && fabsf(sample->ch2_mv) > bounds->ch2_abs_max_mv;
}

bool bzm_telemetry_sample_is_within_bounds(const bzm_telemetry_sample_t * sample, const bzm_telemetry_bounds_t * bounds)
{
    return sample_is_within_non_ch2_bounds(sample, bounds) && fabsf(sample->ch2_mv) <= bounds->ch2_abs_max_mv;
}

bool bzm_telemetry_sample_is_safe(const bzm_telemetry_sample_t * sample, uint64_t now_us, uint64_t max_age_us,
                                  const bzm_telemetry_bounds_t * bounds, bool require_clock_locks)
{
    return sample != NULL && sample->valid && !sample->trip && bzm_telemetry_sample_is_fresh(sample, now_us, max_age_us) &&
           bzm_telemetry_sample_is_within_bounds(sample, bounds) && (!require_clock_locks || sample->pll_locked);
}

void bzm_ch2_confirmation_init(bzm_ch2_confirmation_t * confirmation)
{
    if (confirmation != NULL)
        *confirmation = (bzm_ch2_confirmation_t){0};
}

bzm_ch2_confirmation_result_t bzm_ch2_confirmation_observe(bzm_ch2_confirmation_t * confirmation,
                                                           const bzm_telemetry_store_t * store,
                                                           const bzm_telemetry_bounds_t * bounds,
                                                           uint8_t required_consecutive_samples, uint8_t * culprit_asic_id,
                                                           uint8_t * observed_consecutive_samples)
{
    if (culprit_asic_id != NULL)
        *culprit_asic_id = 0;
    if (observed_consecutive_samples != NULL)
        *observed_consecutive_samples = 0;
    if (confirmation == NULL || store == NULL || !bounds_are_valid(bounds) || required_consecutive_samples == 0 ||
        required_consecutive_samples > BZM_CH2_CONFIRM_MAX_SAMPLES)
        return BZM_CH2_CONFIRMATION_INVALID;

    bool no_new_sample = false;
    for (size_t index = 0; index < BZM_MAX_ASIC_COUNT; ++index) {
        const bzm_telemetry_sample_t * sample = &store->samples[index];
        if (!sample->received || sample->asic_id != bzm_asic_wire_ids[index] || sample->timestamp_us == 0 ||
            !isfinite(sample->ch2_mv) || sample->timestamp_us < confirmation->last_timestamp_us[index]) {
            return BZM_CH2_CONFIRMATION_INVALID;
        }
        if (sample->timestamp_us == confirmation->last_timestamp_us[index])
            no_new_sample = true;
    }

    if (no_new_sample) {
        for (size_t index = 0; index < BZM_MAX_ASIC_COUNT; ++index) {
            if (confirmation->consecutive_excursions[index] != 0) {
                if (culprit_asic_id != NULL)
                    *culprit_asic_id = bzm_asic_wire_ids[index];
                if (observed_consecutive_samples != NULL)
                    *observed_consecutive_samples = confirmation->consecutive_excursions[index];
                break;
            }
        }
        return BZM_CH2_CONFIRMATION_NO_NEW_SAMPLE;
    }

    bzm_ch2_confirmation_result_t result = BZM_CH2_CONFIRMATION_GOOD;
    for (size_t index = 0; index < BZM_MAX_ASIC_COUNT; ++index) {
        const bzm_telemetry_sample_t * sample = &store->samples[index];
        confirmation->last_timestamp_us[index] = sample->timestamp_us;
        if (fabsf(sample->ch2_mv) <= bounds->ch2_abs_max_mv) {
            confirmation->consecutive_excursions[index] = 0;
            continue;
        }

        if (confirmation->consecutive_excursions[index] < UINT8_MAX)
            ++confirmation->consecutive_excursions[index];
        uint8_t count = confirmation->consecutive_excursions[index];
        if (culprit_asic_id != NULL && (*culprit_asic_id == 0 || count >= required_consecutive_samples))
            *culprit_asic_id = bzm_asic_wire_ids[index];
        if (observed_consecutive_samples != NULL && count > *observed_consecutive_samples)
            *observed_consecutive_samples = count;
        if (count >= required_consecutive_samples)
            result = BZM_CH2_CONFIRMATION_CONTINUOUS;
        else if (result == BZM_CH2_CONFIRMATION_GOOD)
            result = BZM_CH2_CONFIRMATION_PENDING;
    }
    return result;
}

void bzm_telemetry_confirmation_init(bzm_telemetry_confirmation_t * confirmation)
{
    if (confirmation != NULL)
        *confirmation = (bzm_telemetry_confirmation_t){0};
}

bzm_ch2_confirmation_result_t bzm_telemetry_confirmation_observe(
    bzm_telemetry_confirmation_t * confirmation, const bzm_telemetry_store_t * store, uint64_t now_us,
    uint64_t max_age_us, const bzm_telemetry_bounds_t * bounds, bool require_clock_locks,
    uint8_t required_consecutive_samples, uint8_t * culprit_asic_id, uint8_t * observed_consecutive_samples)
{
    if (culprit_asic_id != NULL)
        *culprit_asic_id = 0;
    if (observed_consecutive_samples != NULL)
        *observed_consecutive_samples = 0;
    if (confirmation == NULL || store == NULL || now_us == 0 || max_age_us == 0 || !bounds_are_valid(bounds) ||
        required_consecutive_samples == 0 || required_consecutive_samples > BZM_CH2_CONFIRM_MAX_SAMPLES)
        return BZM_CH2_CONFIRMATION_INVALID;

    bool any_new_sample = false;
    bool continuous = false;
    for (size_t index = 0; index < BZM_MAX_ASIC_COUNT; ++index) {
        const bzm_telemetry_sample_t * sample = &store->samples[index];
        if (!sample->received || sample->asic_id != bzm_asic_wire_ids[index] || sample->timestamp_us == 0 ||
            sample->timestamp_us < confirmation->last_timestamp_us[index])
            return BZM_CH2_CONFIRMATION_INVALID;
        if (sample->timestamp_us == confirmation->last_timestamp_us[index])
            continue;

        any_new_sample = true;
        confirmation->last_timestamp_us[index] = sample->timestamp_us;
        if (bzm_telemetry_sample_has_immediate_trip(sample)) {
            confirmation->consecutive_anomalies[index] = required_consecutive_samples;
        } else if (bzm_telemetry_sample_is_safe(sample, now_us, max_age_us, bounds, require_clock_locks)) {
            confirmation->consecutive_anomalies[index] = 0;
        } else if (confirmation->consecutive_anomalies[index] < UINT8_MAX) {
            ++confirmation->consecutive_anomalies[index];
        }
    }

    uint8_t highest_count = 0;
    size_t highest_index = 0;
    for (size_t index = 0; index < BZM_MAX_ASIC_COUNT; ++index) {
        uint8_t count = confirmation->consecutive_anomalies[index];
        if (count > highest_count) {
            highest_count = count;
            highest_index = index;
        }
        if (count >= required_consecutive_samples)
            continuous = true;
    }
    if (highest_count != 0) {
        if (culprit_asic_id != NULL)
            *culprit_asic_id = bzm_asic_wire_ids[highest_index];
        if (observed_consecutive_samples != NULL)
            *observed_consecutive_samples = highest_count;
    }
    if (continuous)
        return BZM_CH2_CONFIRMATION_CONTINUOUS;
    if (!any_new_sample)
        return BZM_CH2_CONFIRMATION_NO_NEW_SAMPLE;
    return highest_count == 0 ? BZM_CH2_CONFIRMATION_GOOD : BZM_CH2_CONFIRMATION_PENDING;
}

void bzm_pll_lock_confirmation_init(bzm_pll_lock_confirmation_t * confirmation)
{
    if (confirmation != NULL)
        *confirmation = (bzm_pll_lock_confirmation_t){0};
}

bzm_ch2_confirmation_result_t bzm_pll_lock_confirmation_observe(
    bzm_pll_lock_confirmation_t * confirmation, const bzm_telemetry_store_t * store, uint64_t now_us,
    uint64_t max_age_us, uint8_t required_consecutive_samples, uint8_t * culprit_asic_id,
    uint8_t * observed_consecutive_samples)
{
    if (culprit_asic_id != NULL)
        *culprit_asic_id = 0;
    if (observed_consecutive_samples != NULL)
        *observed_consecutive_samples = 0;
    if (confirmation == NULL || store == NULL || now_us == 0 || max_age_us == 0 || required_consecutive_samples == 0 ||
        required_consecutive_samples > BZM_CH2_CONFIRM_MAX_SAMPLES)
        return BZM_CH2_CONFIRMATION_INVALID;

    bool any_new_sample = false;
    bool continuous = false;
    uint8_t highest_count = 0;
    size_t highest_index = 0;
    for (size_t index = 0; index < BZM_MAX_ASIC_COUNT; ++index) {
        const bzm_telemetry_sample_t * sample = &store->samples[index];
        if (!sample->received || sample->asic_id != bzm_asic_wire_ids[index] || sample->timestamp_us == 0 ||
            sample->timestamp_us < confirmation->last_timestamp_us[index] ||
            !bzm_telemetry_sample_is_fresh(sample, now_us, max_age_us))
            return BZM_CH2_CONFIRMATION_INVALID;
        if (sample->timestamp_us != confirmation->last_timestamp_us[index]) {
            any_new_sample = true;
            confirmation->last_timestamp_us[index] = sample->timestamp_us;
            if (sample->pll_locked) {
                confirmation->consecutive_unlocks[index] = 0;
            } else if (confirmation->consecutive_unlocks[index] < UINT8_MAX) {
                ++confirmation->consecutive_unlocks[index];
            }
        }
        uint8_t count = confirmation->consecutive_unlocks[index];
        if (count > highest_count) {
            highest_count = count;
            highest_index = index;
        }
        if (count >= required_consecutive_samples)
            continuous = true;
    }

    if (highest_count != 0) {
        if (culprit_asic_id != NULL)
            *culprit_asic_id = bzm_asic_wire_ids[highest_index];
        if (observed_consecutive_samples != NULL)
            *observed_consecutive_samples = highest_count;
    }
    if (continuous)
        return BZM_CH2_CONFIRMATION_CONTINUOUS;
    if (!any_new_sample)
        return BZM_CH2_CONFIRMATION_NO_NEW_SAMPLE;
    return highest_count == 0 ? BZM_CH2_CONFIRMATION_GOOD : BZM_CH2_CONFIRMATION_PENDING;
}
