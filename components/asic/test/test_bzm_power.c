#include <math.h>
#include <string.h>

#include "bzm_power.h"
#include "unity.h"

typedef enum {
    CALL_5V_OFF,
    CALL_5V_ON,
    CALL_REGULATOR_OFF,
    CALL_REGULATOR_ON,
    CALL_VOUT_OFF,
    CALL_VOUT_ON,
    CALL_DELAY,
    CALL_VALIDATE,
} power_call_t;

typedef struct {
    power_call_t calls[24];
    size_t call_count;
    size_t fail_call;
    uint32_t delays[2];
    size_t delay_count;
    float requested_vout;
    float validated_vout;
} simulated_power_t;

static esp_err_t record_call(simulated_power_t *power, power_call_t call)
{
    power->calls[power->call_count++] = call;
    return power->call_count == power->fail_call ? ESP_FAIL : ESP_OK;
}

static esp_err_t simulated_set_5v(void *context, bool enabled)
{
    return record_call(context, enabled ? CALL_5V_ON : CALL_5V_OFF);
}

static esp_err_t simulated_set_regulator(void *context, bool enabled)
{
    return record_call(context,
                       enabled ? CALL_REGULATOR_ON : CALL_REGULATOR_OFF);
}

static esp_err_t simulated_set_vout(void *context, float volts)
{
    simulated_power_t *power = context;
    power->requested_vout = volts;
    return record_call(context, volts == 0.0f ? CALL_VOUT_OFF : CALL_VOUT_ON);
}

static esp_err_t simulated_validate(void *context, float expected_vout)
{
    simulated_power_t *power = context;
    power->validated_vout = expected_vout;
    return record_call(context, CALL_VALIDATE);
}

static void simulated_delay(void *context, uint32_t delay_ms)
{
    simulated_power_t *power = context;
    power->calls[power->call_count++] = CALL_DELAY;
    power->delays[power->delay_count++] = delay_ms;
}

static const bzm_power_ops_t SIMULATED_POWER_OPS = {
    .set_5v_enabled = simulated_set_5v,
    .set_regulator_enabled = simulated_set_regulator,
    .set_vout = simulated_set_vout,
    .validate_power = simulated_validate,
    .delay_ms = simulated_delay,
};

TEST_CASE("BZM TPS profile contains every BIRDS regulator setting",
          "[asic][bzm][power][profile]")
{
    const bzm_tps546_profile_t *p = &BZM_TPS546_BIRDS_PROFILE;
    const uint16_t masks[] = {
        0x0200, 0x1800, 0xe800, 0x0000, 0x0000, 0x0100, 0x4200,
    };
    const uint8_t compensation[] = {0x13, 0x11, 0x8c, 0x1d, 0x06};
    const uint8_t telemetry[] = {0x03, 0x03, 0x03, 0x03, 0x03, 0x00};

    TEST_ASSERT_EQUAL_HEX8(0xff, p->phase);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(masks, p->smbalert_mask, 7);
    TEST_ASSERT_EQUAL(325, p->frequency_switch_khz);
    TEST_ASSERT_EQUAL_HEX8(0x00, p->sync_config);
    TEST_ASSERT_EQUAL_HEX16(0x0000, p->stack_config);
    TEST_ASSERT_EQUAL_HEX16(0x0010, p->interleave);
    TEST_ASSERT_EQUAL_HEX16(0x0000, p->misc_options);
    TEST_ASSERT_EQUAL_HEX16(0x0000, p->pin_detect_override);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(compensation, p->compensation_config, 5);
    TEST_ASSERT_EQUAL_HEX8(0x70, p->power_stage_config);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(telemetry, p->telemetry_config, 6);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, BZM_TPS546_FIXED_VOUT_V,
                             p->vout_command);
    TEST_ASSERT_EQUAL_HEX16(0x0000, p->vout_trim);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, BZM_TPS546_MAX_VOUT_V, p->vout_max);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.1f, p->vout_margin_high);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.9f, p->vout_margin_low);
    TEST_ASSERT_EQUAL_HEX16(0xe010, p->vout_transition_rate);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.125f, p->vout_scale_loop);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, BZM_TPS546_MIN_VOUT_V,
                             p->vout_min);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 11.0f, p->vin_on);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.5f, p->vin_off);
    TEST_ASSERT_EQUAL_HEX16(0xc880, p->iout_cal_gain);
    TEST_ASSERT_EQUAL_HEX16(0xe000, p->iout_cal_offset);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.25f, p->vout_ov_fault_limit);
    TEST_ASSERT_EQUAL_HEX8(0xbd, p->vout_ov_fault_response);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.16f, p->vout_ov_warn_limit);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.90f, p->vout_uv_warn_limit);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.75f, p->vout_uv_fault_limit);
    TEST_ASSERT_EQUAL_HEX8(0xbe, p->vout_uv_fault_response);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 55.0f, p->iout_oc_fault_limit);
    TEST_ASSERT_EQUAL_HEX8(0xc0, p->iout_oc_fault_response);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.0f, p->iout_oc_warn_limit);
    TEST_ASSERT_EQUAL(145, p->ot_fault_limit);
    TEST_ASSERT_EQUAL_HEX8(0xff, p->ot_fault_response);
    TEST_ASSERT_EQUAL(105, p->ot_warn_limit);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 14.0f, p->vin_ov_fault_limit);
    TEST_ASSERT_EQUAL_HEX8(0xb7, p->vin_ov_fault_response);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 11.0f, p->vin_uv_warn_limit);
    TEST_ASSERT_EQUAL(0, p->ton_delay);
    TEST_ASSERT_EQUAL(3, p->ton_rise);
    TEST_ASSERT_EQUAL(0, p->ton_max_fault_limit);
    TEST_ASSERT_EQUAL_HEX8(0x3b, p->ton_max_fault_response);
    TEST_ASSERT_EQUAL(0, p->toff_delay);
    TEST_ASSERT_EQUAL(0, p->toff_fall);
}

TEST_CASE("BZM rail initialization validates power but keeps downstream 5V off",
          "[asic][bzm][power][rail]")
{
    simulated_power_t power = {0};
    const power_call_t expected[] = {
        CALL_5V_OFF, CALL_REGULATOR_ON, CALL_DELAY, CALL_VOUT_ON,
        CALL_DELAY, CALL_VALIDATE,
    };
    TEST_ASSERT_EQUAL(ESP_OK, bzm_power_set_rail_enabled(
        &SIMULATED_POWER_OPS, &power, true));
    TEST_ASSERT_EQUAL_UINT32(sizeof(expected) / sizeof(expected[0]),
                             power.call_count);
    TEST_ASSERT_EQUAL_INT_ARRAY(expected, power.calls, power.call_count);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.8f, power.validated_vout);
}

TEST_CASE("BZM power validation failure reverses the complete sequence",
          "[asic][bzm][power][rollback]")
{
    simulated_power_t power = {.fail_call = 6};
    const power_call_t expected[] = {
        CALL_5V_OFF, CALL_REGULATOR_ON, CALL_DELAY, CALL_VOUT_ON,
        CALL_DELAY, CALL_VALIDATE,
        CALL_5V_OFF, CALL_VOUT_OFF, CALL_REGULATOR_OFF,
    };
    TEST_ASSERT_EQUAL(ESP_FAIL, bzm_power_set_rail_enabled(
        &SIMULATED_POWER_OPS, &power, true));
    TEST_ASSERT_EQUAL_UINT32(sizeof(expected) / sizeof(expected[0]),
                             power.call_count);
    TEST_ASSERT_EQUAL_INT_ARRAY(expected, power.calls, power.call_count);
}

TEST_CASE("BZM runtime rail change validates the requested voltage",
          "[asic][bzm][power][runtime]")
{
    simulated_power_t power = {0};
    const power_call_t expected[] = {
        CALL_VOUT_ON, CALL_DELAY, CALL_VALIDATE,
    };
    TEST_ASSERT_EQUAL(
        ESP_OK,
        bzm_power_set_runtime_voltage(
            &SIMULATED_POWER_OPS, &power, 3.20f));
    TEST_ASSERT_EQUAL_UINT32(
        sizeof(expected) / sizeof(expected[0]), power.call_count);
    TEST_ASSERT_EQUAL_INT_ARRAY(expected, power.calls, power.call_count);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.20f, power.requested_vout);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.20f, power.validated_vout);
    TEST_ASSERT_FALSE(bzm_power_runtime_voltage_is_allowed(2.0f));
    TEST_ASSERT_TRUE(
        bzm_power_runtime_voltage_is_allowed(BZM_TPS546_MIN_VOUT_V));
    TEST_ASSERT_TRUE(bzm_power_runtime_voltage_is_allowed(3.2f));
    TEST_ASSERT_FALSE(bzm_power_runtime_voltage_is_allowed(3.201f));
}

TEST_CASE("BZM user voltage is authoritative within rail bounds",
          "[asic][bzm][power][manual]")
{
    float resolved_v = 0.0f;
    TEST_ASSERT_TRUE(
        bzm_power_resolve_user_voltage(2800, &resolved_v));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.8f, resolved_v);
    TEST_ASSERT_TRUE(
        bzm_power_resolve_user_voltage(2900, &resolved_v));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.9f, resolved_v);
    TEST_ASSERT_TRUE(
        bzm_power_resolve_user_voltage(3200, &resolved_v));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.2f, resolved_v);
    TEST_ASSERT_FALSE(
        bzm_power_resolve_user_voltage(2099, &resolved_v));
    TEST_ASSERT_FALSE(
        bzm_power_resolve_user_voltage(3201, &resolved_v));
    TEST_ASSERT_FALSE(
        bzm_power_resolve_user_voltage(2800, NULL));
}

TEST_CASE("BZM failed runtime rail validation powers down",
          "[asic][bzm][power][runtime][rollback]")
{
    simulated_power_t power = {.fail_call = 3};
    const power_call_t expected[] = {
        CALL_VOUT_ON, CALL_DELAY, CALL_VALIDATE,
        CALL_5V_OFF, CALL_VOUT_OFF, CALL_REGULATOR_OFF,
    };
    TEST_ASSERT_EQUAL(
        ESP_FAIL,
        bzm_power_set_runtime_voltage(
            &SIMULATED_POWER_OPS, &power, 3.0f));
    TEST_ASSERT_EQUAL_UINT32(
        sizeof(expected) / sizeof(expected[0]), power.call_count);
    TEST_ASSERT_EQUAL_INT_ARRAY(expected, power.calls, power.call_count);
}

TEST_CASE("BZM shutdown attempts every safe-state operation after an error",
          "[asic][bzm][power][shutdown]")
{
    simulated_power_t power = {.fail_call = 1};
    const power_call_t expected[] = {
        CALL_5V_OFF, CALL_VOUT_OFF, CALL_REGULATOR_OFF,
    };
    TEST_ASSERT_EQUAL(ESP_FAIL, bzm_power_set_rail_enabled(
        &SIMULATED_POWER_OPS, &power, false));
    TEST_ASSERT_EQUAL_UINT32(sizeof(expected) / sizeof(expected[0]),
                             power.call_count);
    TEST_ASSERT_EQUAL_INT_ARRAY(expected, power.calls, power.call_count);
}
