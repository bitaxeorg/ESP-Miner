#include "bzm_runtime_health.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static __attribute__((format(printf, 3, 4))) bzm_runtime_health_result_t
health_result(bzm_runtime_health_status_t status, bzm_runtime_health_fault_t fault, const char * format, ...)
{
    bzm_runtime_health_result_t result = {
        .status = status,
        .fault = fault,
    };
    if (format != NULL) {
        va_list args;
        va_start(args, format);
        vsnprintf(result.detail, sizeof(result.detail), format, args);
        va_end(args);
    }
    return result;
}

static bzm_runtime_health_result_t good(const char * detail)
{
    return health_result(BZM_RUNTIME_HEALTH_GOOD, BZM_RUNTIME_HEALTH_FAULT_NONE, "%s", detail);
}

static __attribute__((format(printf, 2, 3))) bzm_runtime_health_result_t
bad(bzm_runtime_health_fault_t fault, const char * format, ...)
{
    bzm_runtime_health_result_t result = {
        .status = BZM_RUNTIME_HEALTH_BAD,
        .fault = fault,
    };
    if (format != NULL) {
        va_list args;
        va_start(args, format);
        vsnprintf(result.detail, sizeof(result.detail), format, args);
        va_end(args);
    }
    return result;
}

static bool finite_range(float minimum, float maximum)
{
    return isfinite(minimum) && isfinite(maximum) && minimum <= maximum;
}

static bool in_range(float value, float minimum, float maximum)
{
    return isfinite(value) && finite_range(minimum, maximum) && value >= minimum && value <= maximum;
}

static bzm_runtime_health_result_t check_bridge_and_fan(const bzm_runtime_health_input_t * input)
{
    const bzm_bridge_safety_status_t * status = &input->bridge_status;
    const uint16_t required_capabilities = BZM_BRIDGE_SAFETY_CAP_5V_CONTROL | BZM_BRIDGE_SAFETY_CAP_ASIC_RESET_CONTROL |
                                           BZM_BRIDGE_SAFETY_CAP_FAN_FORCE_FULL | BZM_BRIDGE_SAFETY_CAP_TRIP_INPUT_SAMPLED |
                                           BZM_BRIDGE_SAFETY_CAP_FAN_CONTROLLED_SPEED;
    const uint16_t required_evidence =
        BZM_BRIDGE_SAFETY_EVIDENCE_LEASE_VALID | BZM_BRIDGE_SAFETY_EVIDENCE_TRIP_CLEAR | BZM_BRIDGE_SAFETY_EVIDENCE_FAULT_CLEAR;

    if (!input->bridge_status_available || !status->valid) {
        return bad(BZM_RUNTIME_HEALTH_FAULT_BRIDGE_UNAVAILABLE, "bridge safety status is unavailable");
    }
    if (status->schema_version != BZM_BRIDGE_SAFETY_STATUS_SCHEMA_VERSION || status->stage < BZM_BRIDGE_SAFETY_STAGE_LEASE) {
        return bad(BZM_RUNTIME_HEALTH_FAULT_BRIDGE_STATUS_INVALID, "bridge status schema=%u safetyStage=%u is not lease-capable",
                   (unsigned) status->schema_version, (unsigned) status->stage);
    }
    if ((status->capabilities & required_capabilities) != required_capabilities) {
        return bad(BZM_RUNTIME_HEALTH_FAULT_BRIDGE_CAPABILITY_MISSING, "bridge capabilities=0x%04x missing required=0x%04x",
                   (unsigned) status->capabilities, (unsigned) required_capabilities);
    }
    if (status->state != BZM_BRIDGE_SAFETY_STATE_CONTROLLED) {
        return bad(BZM_RUNTIME_HEALTH_FAULT_BRIDGE_STATE, "bridge state=%u expected CONTROLLED", (unsigned) status->state);
    }
    if (status->fault != BZM_BRIDGE_SAFETY_FAULT_NONE || status->trip_input_asserted ||
        status->runtime_verdict != BZM_BRIDGE_SAFETY_RUNTIME_GOOD_CONTROLLED) {
        return bad(BZM_RUNTIME_HEALTH_FAULT_BRIDGE_FAULT, "bridge fault=%u trip=%u runtimeVerdict=0x%02x", (unsigned) status->fault,
                   (unsigned) status->trip_input_asserted, (unsigned) status->runtime_verdict);
    }
    if (status->lease_remaining_ms == 0 || (status->evidence & required_evidence) != required_evidence) {
        return bad(BZM_RUNTIME_HEALTH_FAULT_BRIDGE_LEASE, "bridge lease=%lu ms evidence=0x%04x required=0x%04x",
                   (unsigned long) status->lease_remaining_ms, (unsigned) status->evidence, (unsigned) required_evidence);
    }

    bool outputs_safe = !status->five_volt_enabled && status->asic_reset_asserted && status->fan_full && status->fan_percent == 100;
    bool outputs_safe_evidence = (status->evidence & BZM_BRIDGE_SAFETY_EVIDENCE_OUTPUTS_SAFE) != 0;
    if (outputs_safe != outputs_safe_evidence) {
        return bad(BZM_RUNTIME_HEALTH_FAULT_BRIDGE_STATUS_INVALID, "bridge output-safe evidence contradicts output readback");
    }
    if (!input->fan_tach_available) {
        return bad(BZM_RUNTIME_HEALTH_FAULT_FAN_TACH_UNAVAILABLE, "fresh fan tach is unavailable");
    }
    if (input->fan_min_rpm == 0) {
        return bad(BZM_RUNTIME_HEALTH_FAULT_INVALID_INPUT, "fan minimum RPM must be nonzero");
    }
    if (input->fan_rpm < input->fan_min_rpm) {
        return bad(BZM_RUNTIME_HEALTH_FAULT_FAN_TACH_LOW, "fan tach=%u RPM below minimum=%u RPM", (unsigned) input->fan_rpm,
                   (unsigned) input->fan_min_rpm);
    }
    return good("bridge lease/status and fan tach are healthy");
}

static bzm_runtime_health_result_t check_power(const bzm_runtime_health_input_t * input)
{
    const bzm_bridge_safety_status_t * bridge = &input->bridge_status;
    if (!bridge->five_volt_enabled || bridge->asic_reset_asserted) {
        return bad(BZM_RUNTIME_HEALTH_FAULT_BRIDGE_OUTPUT,
                   "mining requires bridge 5V on and ASIC reset released");
    }

    if (!finite_range(input->tps_bounds.vin_min_v, input->tps_bounds.vin_max_v) ||
        !isfinite(input->tps_bounds.vout_command_v) || !isfinite(input->tps_bounds.vout_command_tolerance_v) ||
        input->tps_bounds.vout_command_tolerance_v < 0.0f ||
        !finite_range(input->tps_bounds.vout_min_v, input->tps_bounds.vout_max_v) ||
        !finite_range(input->tps_bounds.iout_min_a, input->tps_bounds.iout_max_a) ||
        !finite_range(input->tps_bounds.temperature_min_c, input->tps_bounds.temperature_max_c)) {
        return bad(BZM_RUNTIME_HEALTH_FAULT_INVALID_INPUT, "TPS health bounds are invalid");
    }
    if (!input->tps.available) {
        return bad(BZM_RUNTIME_HEALTH_FAULT_TPS_UNAVAILABLE, "fresh TPS status and telemetry are unavailable");
    }
    /* Classify a finite high-temperature sample before the generic PMBus
     * STATUS_WORD interlock. At the upstream 105 C warning threshold the TPS
     * may assert its temperature summary bit in the same snapshot; it is the
     * one regulator fault eligible for controlled thermal recovery. */
    if (isfinite(input->tps.temperature_c) &&
        input->tps.temperature_c > input->tps_bounds.temperature_max_c) {
        return bad(BZM_RUNTIME_HEALTH_FAULT_TPS_OVERHEAT,
                   "TPS temperature=%.1f C above %.1f C overheat threshold",
                   input->tps.temperature_c,
                   input->tps_bounds.temperature_max_c);
    }
    if (!input->tps.pgood) {
        return bad(BZM_RUNTIME_HEALTH_FAULT_TPS_PGOOD_LOW, "TPS PGOOD is low");
    }
    if ((input->tps.operation & BZM_RUNTIME_HEALTH_TPS_OPERATION_ON_MASK) == 0) {
        return bad(BZM_RUNTIME_HEALTH_FAULT_TPS_OPERATION_OFF, "TPS OPERATION=0x%02x does not command conversion on",
                   (unsigned) input->tps.operation);
    }
    uint16_t fault_bits = input->tps.status_word & BZM_RUNTIME_HEALTH_TPS_STATUS_FAULT_MASK;
    if (fault_bits != 0) {
        return bad(BZM_RUNTIME_HEALTH_FAULT_TPS_STATUS, "TPS STATUS_WORD=0x%04x faultBits=0x%04x",
                   (unsigned) input->tps.status_word, (unsigned) fault_bits);
    }
    if (!input->tps.vout_command_matches_expected ||
        !isfinite(input->tps.vout_command_v) ||
        fabsf(input->tps.vout_command_v - input->tps_bounds.vout_command_v) >
            input->tps_bounds.vout_command_tolerance_v) {
        return bad(BZM_RUNTIME_HEALTH_FAULT_TPS_VOUT_COMMAND,
                   "TPS VOUT_COMMAND=%.3f V exactRaw=%u expected %.3f +/- %.3f V",
                   input->tps.vout_command_v,
                   (unsigned) input->tps.vout_command_matches_expected,
                   input->tps_bounds.vout_command_v,
                   input->tps_bounds.vout_command_tolerance_v);
    }
    if (!in_range(input->tps.vin_v, input->tps_bounds.vin_min_v, input->tps_bounds.vin_max_v)) {
        return bad(BZM_RUNTIME_HEALTH_FAULT_TPS_VIN_RANGE, "TPS VIN=%.3f V outside [%.3f, %.3f] V", input->tps.vin_v,
                   input->tps_bounds.vin_min_v, input->tps_bounds.vin_max_v);
    }
    if (!in_range(input->tps.vout_v, input->tps_bounds.vout_min_v, input->tps_bounds.vout_max_v)) {
        return bad(BZM_RUNTIME_HEALTH_FAULT_TPS_VOUT_RANGE, "TPS VOUT=%.3f V outside [%.3f, %.3f] V", input->tps.vout_v,
                   input->tps_bounds.vout_min_v, input->tps_bounds.vout_max_v);
    }
    if (!in_range(input->tps.iout_a, input->tps_bounds.iout_min_a, input->tps_bounds.iout_max_a)) {
        return bad(BZM_RUNTIME_HEALTH_FAULT_TPS_IOUT_RANGE, "TPS IOUT=%.3f A outside [%.3f, %.3f] A", input->tps.iout_a,
                   input->tps_bounds.iout_min_a, input->tps_bounds.iout_max_a);
    }
    if (!in_range(input->tps.temperature_c, input->tps_bounds.temperature_min_c, input->tps_bounds.temperature_max_c)) {
        return bad(BZM_RUNTIME_HEALTH_FAULT_TPS_TEMPERATURE_RANGE, "TPS temperature=%.1f C outside [%.1f, %.1f] C",
                   input->tps.temperature_c, input->tps_bounds.temperature_min_c, input->tps_bounds.temperature_max_c);
    }
    return good("TPS fixed VOUT command, PGOOD, operation, status, VIN, VOUT and temperature are GOOD");
}

static bool valid_telemetry_bounds(const bzm_telemetry_bounds_t * bounds)
{
    return bounds != NULL && finite_range(bounds->temperature_min_c, bounds->temperature_max_c) &&
           finite_range(bounds->ch0_min_mv, bounds->ch0_max_mv) && finite_range(bounds->ch1_min_mv, bounds->ch1_max_mv) &&
           isfinite(bounds->ch2_abs_max_mv) && bounds->ch2_abs_max_mv >= 0.0f && isfinite(bounds->max_stack_spread_mv) &&
           bounds->max_stack_spread_mv >= 0.0f;
}

static bzm_runtime_health_result_t check_telemetry(const bzm_runtime_health_input_t * input)
{
    if (!input->telemetry_available) {
        return bad(BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_UNAVAILABLE, "four-ASIC telemetry snapshot is unavailable");
    }
    if (!valid_telemetry_bounds(&input->telemetry_bounds)) {
        return bad(BZM_RUNTIME_HEALTH_FAULT_INVALID_INPUT, "ASIC telemetry bounds are invalid");
    }

    for (size_t index = 0; index < BZM_MAX_ASIC_COUNT; ++index) {
        const bzm_telemetry_sample_t * sample = &input->telemetry.samples[index];
        uint8_t expected_id = bzm_asic_wire_ids[index];
        if (!sample->received || sample->asic_id != expected_id) {
            return bad(BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_MISSING, "ASIC 0x%02x telemetry missing or misaddressed as 0x%02x",
                       (unsigned) expected_id, (unsigned) sample->asic_id);
        }
        if (!bzm_telemetry_sample_is_fresh(sample, input->telemetry_now_us, input->telemetry_max_age_us)) {
            return bad(BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_STALE, "ASIC 0x%02x telemetry timestamp=%llu now=%llu maxAge=%llu us",
                       (unsigned) expected_id, (unsigned long long) sample->timestamp_us,
                       (unsigned long long) input->telemetry_now_us, (unsigned long long) input->telemetry_max_age_us);
        }
        const bool finite_thermal_overheat =
            sample->thermal_valid && isfinite(sample->temperature_c) &&
            sample->temperature_c > input->telemetry_bounds.temperature_max_c;
        if (!sample->voltage_trip &&
            (sample->thermal_trip || finite_thermal_overheat)) {
            return bad(BZM_RUNTIME_HEALTH_FAULT_ASIC_OVERHEAT,
                       "ASIC 0x%02x temperature=%.1f C above %.1f C threshold thermalTrip=%u",
                       (unsigned) expected_id, sample->temperature_c,
                       input->telemetry_bounds.temperature_max_c,
                       (unsigned) sample->thermal_trip);
        }
        if (sample->trip || sample->voltage_trip) {
            return bad(BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_TRIP, "ASIC 0x%02x telemetry trip is asserted", (unsigned) expected_id);
        }
        bool safe_with_deferred_ch2 = input->defer_ch2_bounds && bzm_telemetry_sample_is_safe_except_ch2(
                                                                     sample, input->telemetry_now_us, input->telemetry_max_age_us,
                                                                     &input->telemetry_bounds, false);
        if ((!sample->valid || !sample->thermal_valid || !sample->voltage_valid) && !safe_with_deferred_ch2) {
            return bad(BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_INVALID, "ASIC 0x%02x telemetry validity failed", (unsigned) expected_id);
        }
        bool within_bounds = input->defer_ch2_bounds ? safe_with_deferred_ch2
                                                     : bzm_telemetry_sample_is_within_bounds(sample, &input->telemetry_bounds);
        if (!within_bounds) {
            return bad(BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_BOUNDS,
                       "ASIC 0x%02x telemetry outside qualified bounds: temp=%.1f C ch=[%.1f,%.1f,%.1f] mV", (unsigned) expected_id,
                       sample->temperature_c, sample->ch0_mv, sample->ch1_mv, sample->ch2_mv);
        }
        if (!input->defer_clock_locks && !sample->pll_locked) {
            return bad(BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_CLOCK_UNLOCKED, "ASIC 0x%02x combined PLL0/PLL1 telemetry lock is clear",
                       (unsigned) expected_id);
        }
    }
    return good("four fresh safe ASIC samples and clock locks are healthy");
}

bzm_runtime_health_result_t bzm_runtime_health_evaluate(const bzm_runtime_health_input_t *input)
{
    if (input == NULL) {
        return bad(BZM_RUNTIME_HEALTH_FAULT_INVALID_INPUT, "runtime health input is NULL");
    }
    if (!input->running) return good("ASICs are stopped");

    bzm_runtime_health_result_t result = check_bridge_and_fan(input);
    if (result.status != BZM_RUNTIME_HEALTH_GOOD) return result;
    result = check_power(input);
    if (result.status != BZM_RUNTIME_HEALTH_GOOD) return result;
    return check_telemetry(input);
}
