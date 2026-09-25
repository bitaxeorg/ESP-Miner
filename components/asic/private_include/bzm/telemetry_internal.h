#ifndef BZM_TELEMETRY_INTERNAL_H
#define BZM_TELEMETRY_INTERNAL_H

#include "bzm/telemetry.h"
#include "bzm/frame_parser.h"

#define BZM_CH2_CONFIRM_MAX_SAMPLES 10U

typedef struct
{
    uint8_t consecutive_anomalies[BZM_MAX_ASIC_COUNT];
    uint64_t last_timestamp_us[BZM_MAX_ASIC_COUNT];
} bzm_telemetry_confirmation_t;

float bzm_telemetry_temperature_from_code(uint16_t code);
float bzm_telemetry_voltage_from_code_mv(uint16_t code);
bool bzm_telemetry_decode(uint8_t asic_id, const uint8_t * payload, size_t payload_length, uint64_t timestamp_us,
                          bzm_telemetry_sample_t * sample);
void bzm_telemetry_store_init(bzm_telemetry_store_t * store);
bool bzm_telemetry_store_apply_frame(bzm_telemetry_store_t * store, const bzm_frame_t * frame);
bool bzm_telemetry_value_in_bounds(float value, float minimum, float maximum);
bool bzm_telemetry_sample_is_fresh(const bzm_telemetry_sample_t * sample, uint64_t now_us, uint64_t max_age_us);
bool bzm_telemetry_sample_has_immediate_trip(const bzm_telemetry_sample_t * sample);
bool bzm_telemetry_sample_is_safe_except_ch2(const bzm_telemetry_sample_t * sample, uint64_t now_us, uint64_t max_age_us,
                                             const bzm_telemetry_bounds_t * bounds, bool require_clock_locks);
bool bzm_telemetry_sample_is_within_bounds(const bzm_telemetry_sample_t * sample, const bzm_telemetry_bounds_t * bounds);
bool bzm_telemetry_sample_is_safe(const bzm_telemetry_sample_t * sample, uint64_t now_us, uint64_t max_age_us,
                                  const bzm_telemetry_bounds_t * bounds, bool require_clock_locks);
void bzm_telemetry_confirmation_init(bzm_telemetry_confirmation_t * confirmation);
bzm_ch2_confirmation_result_t bzm_telemetry_confirmation_observe(
    bzm_telemetry_confirmation_t * confirmation, const bzm_telemetry_store_t * store, uint64_t now_us,
    uint64_t max_age_us, const bzm_telemetry_bounds_t * bounds, bool require_clock_locks,
    uint8_t required_consecutive_samples, uint8_t * culprit_asic_id, uint8_t * observed_consecutive_samples);

#endif // BZM_TELEMETRY_INTERNAL_H
