#ifndef BZM_POWER_H
#define BZM_POWER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define BZM_TPS546_FIXED_VOUT_V 2.8f
#define BZM_TPS546_MIN_VOUT_V 2.1f
/* Four series BZM chips are limited to 0.81 V each on average (3.24 V
 * aggregate). Keep 40 mV of command margin below that absolute limit. */
#define BZM_TPS546_MAX_VOUT_V 3.2f
#define BZM_TPS546_VOUT_TOLERANCE_V 0.001f
/* Extended VOUT_MODE uses a 1.953125 mV ULINEAR16 LSB and the TPS helper
 * truncates when encoding 2.800 V, yielding a decoded 2.798828 V. Exact raw
 * word equality is enforced separately; this bound covers representation
 * error only. */
#define BZM_TPS546_VOUT_READBACK_TOLERANCE_V 0.002f
#define BZM_TPS546_VOUT_OPERATING_TOLERANCE_V 0.15f

typedef struct {
    uint8_t phase;
    uint16_t smbalert_mask[7];
    int frequency_switch_khz;
    uint8_t sync_config;
    uint16_t stack_config;
    uint16_t interleave;
    uint16_t misc_options;
    uint16_t pin_detect_override;
    uint8_t compensation_config[5];
    uint8_t power_stage_config;
    uint8_t telemetry_config[6];
    float vout_command;
    uint16_t vout_trim;
    float vout_max;
    float vout_margin_high;
    float vout_margin_low;
    uint16_t vout_transition_rate;
    float vout_scale_loop;
    float vout_min;
    float vin_on;
    float vin_off;
    uint16_t iout_cal_gain;
    uint16_t iout_cal_offset;
    float vout_ov_fault_limit;
    uint8_t vout_ov_fault_response;
    float vout_ov_warn_limit;
    float vout_uv_warn_limit;
    float vout_uv_fault_limit;
    uint8_t vout_uv_fault_response;
    float iout_oc_fault_limit;
    uint8_t iout_oc_fault_response;
    float iout_oc_warn_limit;
    int ot_fault_limit;
    uint8_t ot_fault_response;
    int ot_warn_limit;
    float vin_ov_fault_limit;
    uint8_t vin_ov_fault_response;
    float vin_uv_warn_limit;
    int ton_delay;
    int ton_rise;
    int ton_max_fault_limit;
    uint8_t ton_max_fault_response;
    int toff_delay;
    int toff_fall;
} bzm_tps546_profile_t;

extern const bzm_tps546_profile_t BZM_TPS546_BIRDS_PROFILE;

bool bzm_power_runtime_voltage_is_allowed(float volts);
bool bzm_power_resolve_user_voltage(uint16_t millivolts, float *volts);

typedef struct {
    esp_err_t (*set_5v_enabled)(void *context, bool enabled);
    esp_err_t (*set_regulator_enabled)(void *context, bool enabled);
    esp_err_t (*set_vout)(void *context, float volts);
    esp_err_t (*validate_power)(void *context, float expected_vout);
    void (*delay_ms)(void *context, uint32_t delay_ms);
} bzm_power_ops_t;

esp_err_t bzm_power_set_rail_enabled(const bzm_power_ops_t *ops,
                                     void *context, bool enabled);
esp_err_t bzm_power_set_runtime_voltage(const bzm_power_ops_t *ops,
                                        void *context, float volts);

#endif // BZM_POWER_H
