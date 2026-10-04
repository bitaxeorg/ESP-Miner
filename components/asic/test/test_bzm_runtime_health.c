#include "bzm/bridge_protocol.h"
#include "bzm/telemetry_internal.h"
#include <math.h>
#include <string.h>

#include "bzm/runtime_health.h"
#include "unity.h"

static bzm_runtime_health_input_t good_input(void)
{
    bzm_runtime_health_input_t input;
    memset(&input, 0, sizeof(input));
    input.running = true;
    input.bridge_status_available = true;
    input.bridge_status = (bzm_bridge_safety_status_t){
        .valid = true,
        .schema_version = BZM_BRIDGE_SAFETY_STATUS_SCHEMA_VERSION,
        .stage = BZM_BRIDGE_SAFETY_STAGE_LEASE,
        .state = BZM_BRIDGE_SAFETY_STATE_CONTROLLED,
        .fault = BZM_BRIDGE_SAFETY_FAULT_NONE,
        .runtime_verdict = BZM_BRIDGE_SAFETY_RUNTIME_GOOD_CONTROLLED,
        .production_verdict = BZM_BRIDGE_SAFETY_PRODUCTION_BAD_STAGE_DISABLED,
        .capabilities = BZM_BRIDGE_SAFETY_CAP_5V_CONTROL | BZM_BRIDGE_SAFETY_CAP_ASIC_RESET_CONTROL |
                        BZM_BRIDGE_SAFETY_CAP_FAN_FORCE_FULL | BZM_BRIDGE_SAFETY_CAP_TRIP_INPUT_SAMPLED |
                        BZM_BRIDGE_SAFETY_CAP_FAN_CONTROLLED_SPEED,
        .evidence =
            BZM_BRIDGE_SAFETY_EVIDENCE_LEASE_VALID | BZM_BRIDGE_SAFETY_EVIDENCE_TRIP_CLEAR | BZM_BRIDGE_SAFETY_EVIDENCE_FAULT_CLEAR,
        .lease_remaining_ms = 5000,
        .five_volt_enabled = true,
        .fan_full = true,
        .fan_percent = 100,
    };
    input.fan_tach_available = true;
    input.fan_rpm = 2200;
    input.fan_min_rpm = 1000;

    input.tps = (bzm_runtime_health_tps_sample_t){
        .available = true,
        .pgood = true,
        .operation = BZM_RUNTIME_HEALTH_TPS_OPERATION_ON_MASK,
        .status_word = 0,
        .vout_command_v = 2.8f,
        .vout_command_matches_expected = true,
        .vin_v = 12.0f,
        .vout_v = 2.8f,
        .iout_a = 10.0f,
        .temperature_c = 55.0f,
    };
    input.tps_bounds = (bzm_runtime_health_tps_bounds_t){
        .vin_min_v = 10.5f,
        .vin_max_v = 15.0f,
        .vout_command_v = 2.8f,
        .vout_command_tolerance_v = 0.002f,
        .vout_min_v = 2.65f,
        .vout_max_v = 2.95f,
        .iout_min_a = -1.0f,
        .iout_max_a = 50.0f,
        .temperature_min_c = -40.0f,
        .temperature_max_c = 105.0f,
    };

    input.telemetry_available = true;
    input.telemetry_bounds = (bzm_telemetry_bounds_t){
        .temperature_min_c = -20.0f,
        .temperature_max_c = 75.0f,
        .ch0_min_mv = 300.0f,
        .ch0_max_mv = 800.0f,
        .ch1_min_mv = 300.0f,
        .ch1_max_mv = 800.0f,
        .ch2_abs_max_mv = 50.0f,
        .max_stack_spread_mv = 100.0f,
    };
    input.telemetry_now_us = 1000000;
    input.telemetry_max_age_us = 200000;
    bzm_telemetry_store_init(&input.telemetry);
    for (size_t index = 0; index < BZM_MAX_ASIC_COUNT; ++index) {
        input.telemetry.samples[index] = (bzm_telemetry_sample_t){
            .asic_id = bzm_asic_wire_ids[index],
            .timestamp_us = 900000,
            .received = true,
            .temperature_c = 50.0f,
            .thermal_enabled = true,
            .thermal_validity = true,
            .thermal_valid = true,
            .ch0_mv = 400.0f,
            .ch1_mv = 410.0f,
            .ch2_mv = 1.0f,
            .voltage_enabled = true,
            .voltage_valid = true,
            .pll_locked = true,
            .valid = true,
        };
    }
    return input;
}

static void assert_fault(const bzm_runtime_health_input_t * input, bzm_runtime_health_fault_t fault)
{
    bzm_runtime_health_result_t result = bzm_runtime_health_evaluate(input);
    TEST_ASSERT_EQUAL(BZM_RUNTIME_HEALTH_BAD, result.status);
    TEST_ASSERT_EQUAL_HEX16(fault, result.fault);
    TEST_ASSERT_NOT_EQUAL('\0', result.detail[0]);
    TEST_ASSERT_EQUAL_CHAR('\0', result.detail[sizeof(result.detail) - 1]);
}

TEST_CASE("BZM runtime health rejects invalid active input", "[asic][bzm][runtime-health]")
{
    assert_fault(NULL, BZM_RUNTIME_HEALTH_FAULT_INVALID_INPUT);
    bzm_runtime_health_input_t input = good_input();
    input.fan_min_rpm = 0;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_INVALID_INPUT);
}

TEST_CASE("BZM runtime health fails closed on bridge safety evidence", "[asic][bzm][runtime-health]")
{
    bzm_runtime_health_input_t input = good_input();
    input.bridge_status_available = false;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_BRIDGE_UNAVAILABLE);

    input = good_input();
    input.bridge_status.schema_version++;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_BRIDGE_STATUS_INVALID);

    input = good_input();
    input.bridge_status.stage = BZM_BRIDGE_SAFETY_STAGE_BOOT_SAFE;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_BRIDGE_STATUS_INVALID);

    input = good_input();
    input.bridge_status.capabilities &= (uint16_t) ~BZM_BRIDGE_SAFETY_CAP_TRIP_INPUT_SAMPLED;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_BRIDGE_CAPABILITY_MISSING);

    input = good_input();
    input.bridge_status.capabilities &=
        (uint16_t) ~BZM_BRIDGE_SAFETY_CAP_FAN_CONTROLLED_SPEED;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_BRIDGE_CAPABILITY_MISSING);

    input = good_input();
    input.bridge_status.state = BZM_BRIDGE_SAFETY_STATE_SAFE_OFF;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_BRIDGE_STATE);

    input = good_input();
    input.bridge_status.fault = BZM_BRIDGE_SAFETY_FAULT_LEASE_EXPIRED;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_BRIDGE_FAULT);

    input = good_input();
    input.bridge_status.trip_input_asserted = true;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_BRIDGE_FAULT);

    input = good_input();
    input.bridge_status.lease_remaining_ms = 0;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_BRIDGE_LEASE);

    input = good_input();
    input.bridge_status.evidence &= (uint16_t) ~BZM_BRIDGE_SAFETY_EVIDENCE_TRIP_CLEAR;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_BRIDGE_LEASE);

    input = good_input();
    input.bridge_status.evidence |= BZM_BRIDGE_SAFETY_EVIDENCE_OUTPUTS_SAFE;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_BRIDGE_STATUS_INVALID);

}

TEST_CASE("BZM runtime health enforces fan command and tach threshold", "[asic][bzm][runtime-health]")
{
    bzm_runtime_health_input_t input = good_input();
    input.fan_tach_available = false;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_FAN_TACH_UNAVAILABLE);

    input = good_input();
    input.fan_rpm = input.fan_min_rpm - 1;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_FAN_TACH_LOW);

    input.fan_rpm = input.fan_min_rpm;
    TEST_ASSERT_EQUAL(BZM_RUNTIME_HEALTH_GOOD, bzm_runtime_health_evaluate(&input).status);

    input = good_input();
    input.bridge_status.fan_full = false;
    input.bridge_status.fan_percent = 35;
    TEST_ASSERT_EQUAL(BZM_RUNTIME_HEALTH_GOOD,
                      bzm_runtime_health_evaluate(&input).status);

    input.fan_rpm = input.fan_min_rpm - 1;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_FAN_TACH_LOW);
}

TEST_CASE("BZM runtime health validates every TPS runtime invariant", "[asic][bzm][runtime-health]")
{
    bzm_runtime_health_input_t input = good_input();
    input.tps.available = false;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TPS_UNAVAILABLE);

    input = good_input();
    input.tps.pgood = false;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TPS_PGOOD_LOW);

    input = good_input();
    input.tps.operation = 0;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TPS_OPERATION_OFF);

    input = good_input();
    input.tps.status_word = 0x0040;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TPS_STATUS);

    input = good_input();
    input.tps.status_word = 0x1000;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TPS_STATUS);

    input = good_input();
    input.tps.vout_command_matches_expected = false;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TPS_VOUT_COMMAND);

    input = good_input();
    input.tps.vout_command_v = 2.81f;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TPS_VOUT_COMMAND);

    input = good_input();
    input.tps.vout_command_v = NAN;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TPS_VOUT_COMMAND);

    input = good_input();
    input.tps.vin_v = NAN;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TPS_VIN_RANGE);
    input = good_input();
    input.tps.vin_v = input.tps_bounds.vin_max_v + 0.1f;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TPS_VIN_RANGE);

    input = good_input();
    input.tps.vout_v = input.tps_bounds.vout_min_v - 0.01f;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TPS_VOUT_RANGE);

    input = good_input();
    input.tps.iout_a = input.tps_bounds.iout_max_a + 0.1f;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TPS_IOUT_RANGE);

    input = good_input();
    input.tps.iout_a = NAN;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TPS_IOUT_RANGE);

    input = good_input();
    input.tps.temperature_c = input.tps_bounds.temperature_max_c + 1.0f;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TPS_OVERHEAT);

    input = good_input();
    input.tps.temperature_c = input.tps_bounds.temperature_max_c + 1.0f;
    input.tps.status_word = 0x0004;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TPS_OVERHEAT);

    input = good_input();
    input.tps.temperature_c = NAN;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TPS_TEMPERATURE_RANGE);

    input = good_input();
    input.tps_bounds.vout_min_v = input.tps_bounds.vout_max_v + 1.0f;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_INVALID_INPUT);

    input = good_input();
    input.tps_bounds.vout_command_tolerance_v = -0.001f;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_INVALID_INPUT);
}

TEST_CASE("BZM runtime health requires four fresh valid bounded ASIC samples", "[asic][bzm][runtime-health]")
{
    bzm_runtime_health_input_t input = good_input();
    input.telemetry_available = false;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_UNAVAILABLE);

    input = good_input();
    input.telemetry.samples[2].received = false;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_MISSING);

    input = good_input();
    input.telemetry.samples[1].asic_id = BZM_FIRST_ASIC_ID;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_MISSING);

    input = good_input();
    input.telemetry.samples[0].trip = true;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_TRIP);

    input = good_input();
    input.telemetry.samples[0].valid = false;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_INVALID);

    input = good_input();
    input.telemetry.samples[3].timestamp_us = input.telemetry_now_us - input.telemetry_max_age_us - 1;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_STALE);

    input = good_input();
    input.telemetry.samples[3].timestamp_us = input.telemetry_now_us + 1;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_STALE);

    input = good_input();
    input.telemetry.samples[2].ch2_mv = input.telemetry_bounds.ch2_abs_max_mv + 1.0f;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_BOUNDS);

    input = good_input();
    input.defer_ch2_bounds = true;
    input.telemetry.samples[2].ch2_mv = input.telemetry_bounds.ch2_abs_max_mv + 1.0f;
    TEST_ASSERT_EQUAL(BZM_RUNTIME_HEALTH_GOOD, bzm_runtime_health_evaluate(&input).status);

    input.telemetry.samples[2].voltage_fault = true;
    input.telemetry.samples[2].voltage_valid = false;
    input.telemetry.samples[2].valid = false;
    TEST_ASSERT_EQUAL(BZM_RUNTIME_HEALTH_GOOD, bzm_runtime_health_evaluate(&input).status);

    input.telemetry.samples[2].ch2_mv = 0.0f;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_INVALID);

    input = good_input();
    input.defer_ch2_bounds = true;
    input.telemetry.samples[2].ch0_mv = input.telemetry_bounds.ch0_min_mv - 1.0f;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_BOUNDS);

    input = good_input();
    input.telemetry.samples[2].ch0_mv = 300.0f;
    input.telemetry.samples[2].ch1_mv = 400.1f;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_BOUNDS);

    input = good_input();
    input.telemetry.samples[2].temperature_c = NAN;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_BOUNDS);

    input = good_input();
    input.telemetry.samples[2].temperature_c =
        input.telemetry_bounds.temperature_max_c;
    TEST_ASSERT_EQUAL(BZM_RUNTIME_HEALTH_GOOD,
                      bzm_runtime_health_evaluate(&input).status);

    input.telemetry.samples[2].temperature_c =
        input.telemetry_bounds.temperature_max_c + 0.1f;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_ASIC_OVERHEAT);

    input = good_input();
    input.telemetry.samples[2].thermal_trip = true;
    input.telemetry.samples[2].trip = true;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_ASIC_OVERHEAT);

    input = good_input();
    input.telemetry.samples[2].thermal_trip = true;
    input.telemetry.samples[2].voltage_trip = true;
    input.telemetry.samples[2].trip = true;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_TRIP);

    input = good_input();
    input.telemetry_bounds.ch1_min_mv = input.telemetry_bounds.ch1_max_mv + 1.0f;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_INVALID_INPUT);

    input = good_input();
    input.telemetry_bounds.max_stack_spread_mv = -1.0f;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_INVALID_INPUT);
}

TEST_CASE("BZM runtime health requires locked PLL telemetry while mining", "[asic][bzm][runtime-health]")
{
    bzm_runtime_health_input_t input = good_input();
    input.telemetry.samples[0].pll_locked = false;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_TELEMETRY_CLOCK_UNLOCKED);

    input.defer_clock_locks = true;
    TEST_ASSERT_EQUAL(BZM_RUNTIME_HEALTH_GOOD, bzm_runtime_health_evaluate(&input).status);
}

TEST_CASE("BZM runtime health checks hardware only while running", "[asic][bzm][runtime-health]")
{
    bzm_runtime_health_input_t input = {0};
    TEST_ASSERT_EQUAL(BZM_RUNTIME_HEALTH_GOOD, bzm_runtime_health_evaluate(&input).status);
    input = good_input();
    TEST_ASSERT_EQUAL(BZM_RUNTIME_HEALTH_GOOD, bzm_runtime_health_evaluate(&input).status);
    input.bridge_status.five_volt_enabled = false;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_BRIDGE_OUTPUT);
    input = good_input();
    input.bridge_status.asic_reset_asserted = true;
    assert_fault(&input, BZM_RUNTIME_HEALTH_FAULT_BRIDGE_OUTPUT);
}
