/**
 * @file test_power_management_calc.c
 * @brief Unit tests for power management calculation functions
 */

#include "unity.h"
#include "power_management_calc.h"
#include <math.h>

/* Helper macro for float comparison */
#define FLOAT_DELTA 0.01f

/* ============================================
 * Fan Speed Calculation Tests
 * ============================================ */

static void test_fan_speed_below_min_temp(void)
{
    /* Below minimum temp (45C), should return minimum fan speed (35%) */
    float speed = pm_calc_fan_speed_percent(30.0f);
    TEST_ASSERT_FLOAT_WITHIN(FLOAT_DELTA, PM_MIN_FAN_SPEED, speed);

    speed = pm_calc_fan_speed_percent(44.9f);
    TEST_ASSERT_FLOAT_WITHIN(FLOAT_DELTA, PM_MIN_FAN_SPEED, speed);
}

static void test_fan_speed_at_min_temp(void)
{
    /* At exactly minimum temp, should return minimum fan speed */
    float speed = pm_calc_fan_speed_percent(PM_MIN_FAN_TEMP);
    TEST_ASSERT_FLOAT_WITHIN(FLOAT_DELTA, PM_MIN_FAN_SPEED, speed);
}

static void test_fan_speed_at_throttle_temp(void)
{
    /* At throttle temp (75C), should return 100% */
    float speed = pm_calc_fan_speed_percent(PM_THROTTLE_TEMP);
    TEST_ASSERT_FLOAT_WITHIN(FLOAT_DELTA, 100.0f, speed);
}

static void test_fan_speed_above_throttle_temp(void)
{
    /* Above throttle temp, should return 100% */
    float speed = pm_calc_fan_speed_percent(80.0f);
    TEST_ASSERT_FLOAT_WITHIN(FLOAT_DELTA, 100.0f, speed);

    speed = pm_calc_fan_speed_percent(100.0f);
    TEST_ASSERT_FLOAT_WITHIN(FLOAT_DELTA, 100.0f, speed);
}

static void test_fan_speed_linear_interpolation(void)
{
    /* Midpoint between 45C and 75C is 60C
     * Fan range is 35% to 100% = 65% range
     * At midpoint, should be 35% + 32.5% = 67.5% */
    float midpoint_temp = (PM_MIN_FAN_TEMP + PM_THROTTLE_TEMP) / 2.0f;
    float expected = PM_MIN_FAN_SPEED + (100.0f - PM_MIN_FAN_SPEED) / 2.0f;

    float speed = pm_calc_fan_speed_percent(midpoint_temp);
    TEST_ASSERT_FLOAT_WITHIN(FLOAT_DELTA, expected, speed);
}

static void test_fan_speed_custom_params(void)
{
    /* Test with custom parameters */
    float speed = pm_calc_fan_speed_percent_ex(50.0f, 40.0f, 80.0f, 20.0f);

    /* 50C is 25% of the way from 40 to 80 (10/40)
     * Fan range is 20% to 100% = 80%
     * Expected: 20% + (10/40 * 80%) = 20% + 20% = 40% */
    TEST_ASSERT_FLOAT_WITHIN(FLOAT_DELTA, 40.0f, speed);
}

/* ============================================
 * Overheat Detection Tests
 * ============================================ */

static void test_overheat_none(void)
{
    pm_overheat_type_t result = pm_check_overheat(60.0f, 80.0f,
                                                   PM_THROTTLE_TEMP,
                                                   PM_TPS546_THROTTLE_TEMP);
    TEST_ASSERT_EQUAL(PM_OVERHEAT_NONE, result);
}

static void test_overheat_chip_only(void)
{
    pm_overheat_type_t result = pm_check_overheat(80.0f, 80.0f,
                                                   PM_THROTTLE_TEMP,
                                                   PM_TPS546_THROTTLE_TEMP);
    TEST_ASSERT_EQUAL(PM_OVERHEAT_CHIP, result);
}

static void test_overheat_vr_only(void)
{
    pm_overheat_type_t result = pm_check_overheat(60.0f, 110.0f,
                                                   PM_THROTTLE_TEMP,
                                                   PM_TPS546_THROTTLE_TEMP);
    TEST_ASSERT_EQUAL(PM_OVERHEAT_VR, result);
}

static void test_overheat_both(void)
{
    pm_overheat_type_t result = pm_check_overheat(80.0f, 110.0f,
                                                   PM_THROTTLE_TEMP,
                                                   PM_TPS546_THROTTLE_TEMP);
    TEST_ASSERT_EQUAL(PM_OVERHEAT_BOTH, result);
}

static void test_overheat_vr_zero_ignored(void)
{
    /* VR temp of 0 should be ignored (sensor not available) */
    pm_overheat_type_t result = pm_check_overheat(60.0f, 0.0f,
                                                   PM_THROTTLE_TEMP,
                                                   PM_TPS546_THROTTLE_TEMP);
    TEST_ASSERT_EQUAL(PM_OVERHEAT_NONE, result);
}

static void test_should_trigger_overheat_yes(void)
{
    bool result = pm_should_trigger_overheat(80.0f, 0.0f, 500, 1200);
    TEST_ASSERT_TRUE(result);
}

static void test_should_trigger_overheat_no_safe_values(void)
{
    /* Already at safe values (freq <= 50, voltage <= 1000) */
    bool result = pm_should_trigger_overheat(80.0f, 0.0f, 50, 1000);
    TEST_ASSERT_FALSE(result);
}

static void test_should_trigger_overheat_no_normal_temp(void)
{
    bool result = pm_should_trigger_overheat(60.0f, 80.0f, 500, 1200);
    TEST_ASSERT_FALSE(result);
}

static void test_overheat_severity_soft(void)
{
    /* Events 1, 2, 4, 5, 7, 8 should be soft recovery */
    TEST_ASSERT_EQUAL(PM_SEVERITY_SOFT, pm_calc_overheat_severity(0));  /* Will be event 1 */
    TEST_ASSERT_EQUAL(PM_SEVERITY_SOFT, pm_calc_overheat_severity(1));  /* Will be event 2 */
    TEST_ASSERT_EQUAL(PM_SEVERITY_SOFT, pm_calc_overheat_severity(3));  /* Will be event 4 */
    TEST_ASSERT_EQUAL(PM_SEVERITY_SOFT, pm_calc_overheat_severity(4));  /* Will be event 5 */
}

static void test_overheat_severity_hard(void)
{
    /* Events 3, 6, 9 should be hard recovery */
    TEST_ASSERT_EQUAL(PM_SEVERITY_HARD, pm_calc_overheat_severity(2));  /* Will be event 3 */
    TEST_ASSERT_EQUAL(PM_SEVERITY_HARD, pm_calc_overheat_severity(5));  /* Will be event 6 */
    TEST_ASSERT_EQUAL(PM_SEVERITY_HARD, pm_calc_overheat_severity(8));  /* Will be event 9 */
}

/* ============================================
 * Autotune Calculation Tests
 * ============================================ */

static void test_autotune_skip_invalid_temp(void)
{
    pm_autotune_input_t input = {
        .chip_temp = 255.0f,  /* Invalid reading */
        .current_hashrate = 500.0f,
        .target_hashrate = 500.0f,
        .current_frequency = 500,
        .current_voltage = 1200,
        .current_power = 15,
        .uptime_seconds = 1000
    };
    pm_autotune_limits_t limits = {
        .max_frequency = 800, .min_frequency = 300,
        .max_voltage = 1400, .min_voltage = 1000,
        .max_power = 25
    };

    pm_autotune_decision_t result = pm_calc_autotune(&input, &limits, 60, 0, 400000);

    TEST_ASSERT_TRUE(result.skip_reason_invalid);
    TEST_ASSERT_FALSE(result.should_adjust);
}

static void test_autotune_skip_zero_hashrate(void)
{
    pm_autotune_input_t input = {
        .chip_temp = 60.0f,
        .current_hashrate = 0.0f,  /* Zero hashrate */
        .target_hashrate = 500.0f,
        .current_frequency = 500,
        .current_voltage = 1200,
        .current_power = 15,
        .uptime_seconds = 1000
    };
    pm_autotune_limits_t limits = {
        .max_frequency = 800, .min_frequency = 300,
        .max_voltage = 1400, .min_voltage = 1000,
        .max_power = 25
    };

    pm_autotune_decision_t result = pm_calc_autotune(&input, &limits, 60, 0, 400000);

    TEST_ASSERT_TRUE(result.skip_reason_invalid);
}

static void test_autotune_skip_warmup(void)
{
    pm_autotune_input_t input = {
        .chip_temp = 50.0f,  /* Below target */
        .current_hashrate = 500.0f,
        .target_hashrate = 500.0f,
        .current_frequency = 500,
        .current_voltage = 1200,
        .current_power = 15,
        .uptime_seconds = 500  /* Only 500 seconds, need 900 */
    };
    pm_autotune_limits_t limits = {
        .max_frequency = 800, .min_frequency = 300,
        .max_voltage = 1400, .min_voltage = 1000,
        .max_power = 25
    };

    pm_autotune_decision_t result = pm_calc_autotune(&input, &limits, 60, 0, 400000);

    TEST_ASSERT_TRUE(result.skip_reason_warmup);
    TEST_ASSERT_FALSE(result.should_adjust);
}

static void test_autotune_skip_timing(void)
{
    pm_autotune_input_t input = {
        .chip_temp = 60.0f,
        .current_hashrate = 500.0f,
        .target_hashrate = 500.0f,
        .current_frequency = 500,
        .current_voltage = 1200,
        .current_power = 15,
        .uptime_seconds = 1000
    };
    pm_autotune_limits_t limits = {
        .max_frequency = 800, .min_frequency = 300,
        .max_voltage = 1400, .min_voltage = 1000,
        .max_power = 25
    };

    /* At 60C, interval should be 5 minutes (300000ms) */
    pm_autotune_decision_t result = pm_calc_autotune(&input, &limits, 60, 0, 100000);

    TEST_ASSERT_TRUE(result.skip_reason_timing);
}

static void test_autotune_reset_preset_on_low_hashrate(void)
{
    pm_autotune_input_t input = {
        .chip_temp = 60.0f,
        .current_hashrate = 200.0f,  /* Low hashrate */
        .target_hashrate = 500.0f,
        .current_frequency = 500,
        .current_voltage = 1200,
        .current_power = 15,
        .uptime_seconds = 1000
    };
    pm_autotune_limits_t limits = {
        .max_frequency = 800, .min_frequency = 300,
        .max_voltage = 1400, .min_voltage = 1000,
        .max_power = 25
    };

    /* 3 consecutive low hashrate events should trigger reset */
    pm_autotune_decision_t result = pm_calc_autotune(&input, &limits, 60, 3, 400000);

    TEST_ASSERT_TRUE(result.should_reset_preset);
}

static void test_autotune_increase_freq_when_cold(void)
{
    pm_autotune_input_t input = {
        .chip_temp = 50.0f,  /* Well below target (60) */
        .current_hashrate = 500.0f,
        .target_hashrate = 500.0f,
        .current_frequency = 500,
        .current_voltage = 1200,
        .current_power = 15,
        .uptime_seconds = 1000
    };
    pm_autotune_limits_t limits = {
        .max_frequency = 800, .min_frequency = 300,
        .max_voltage = 1400, .min_voltage = 1000,
        .max_power = 25
    };

    pm_autotune_decision_t result = pm_calc_autotune(&input, &limits, 60, 0, 400000);

    TEST_ASSERT_TRUE(result.should_adjust);
    TEST_ASSERT_GREATER_THAN(input.current_frequency, result.new_frequency);
}

static void test_autotune_decrease_freq_when_hot(void)
{
    pm_autotune_input_t input = {
        .chip_temp = 70.0f,  /* Above target (60) */
        .current_hashrate = 500.0f,
        .target_hashrate = 500.0f,
        .current_frequency = 500,
        .current_voltage = 1200,
        .current_power = 15,
        .uptime_seconds = 1000
    };
    pm_autotune_limits_t limits = {
        .max_frequency = 800, .min_frequency = 300,
        .max_voltage = 1400, .min_voltage = 1000,
        .max_power = 25
    };

    /* At 70C, interval is 500ms */
    pm_autotune_decision_t result = pm_calc_autotune(&input, &limits, 60, 0, 1000);

    TEST_ASSERT_TRUE(result.should_adjust);
    TEST_ASSERT_LESS_THAN(input.current_frequency, result.new_frequency);
}

static void test_autotune_respects_freq_limits(void)
{
    pm_autotune_input_t input = {
        .chip_temp = 70.0f,
        .current_hashrate = 500.0f,
        .target_hashrate = 500.0f,
        .current_frequency = 305,  /* Near minimum */
        .current_voltage = 1200,
        .current_power = 15,
        .uptime_seconds = 1000
    };
    pm_autotune_limits_t limits = {
        .max_frequency = 800, .min_frequency = 300,
        .max_voltage = 1400, .min_voltage = 1000,
        .max_power = 25
    };

    pm_autotune_decision_t result = pm_calc_autotune(&input, &limits, 60, 0, 1000);

    /* Should not go below minimum */
    if (result.new_frequency > 0) {
        TEST_ASSERT_GREATER_OR_EQUAL(limits.min_frequency, result.new_frequency);
    }
}

/* ============================================
 * Utility Function Tests
 * ============================================ */

static void test_hashrate_low_detection(void)
{
    /* 200 GH/s is 40% of 500 GH/s target, below 50% threshold */
    TEST_ASSERT_TRUE(pm_is_hashrate_low(200.0f, 500.0f, 50.0f));

    /* 300 GH/s is 60% of 500 GH/s target, above 50% threshold */
    TEST_ASSERT_FALSE(pm_is_hashrate_low(300.0f, 500.0f, 50.0f));
}

static void test_target_hashrate_calculation(void)
{
    /* 500 MHz * (672 cores * 1 ASIC) / 1000 = 336 GH/s */
    float target = pm_calc_target_hashrate(500, 672, 1);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 336.0f, target);
}

static void test_efficiency_calculation(void)
{
    /* 15W / (500 GH/s / 1000) = 15W / 0.5 TH/s = 30 J/TH */
    float efficiency = pm_calc_efficiency(15.0f, 500.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 30.0f, efficiency);
}

static void test_efficiency_zero_hashrate(void)
{
    float efficiency = pm_calc_efficiency(15.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(FLOAT_DELTA, 0.0f, efficiency);
}

static void test_autotune_interval_normal_temp(void)
{
    uint32_t interval = pm_get_autotune_interval_ms(50.0f);
    TEST_ASSERT_EQUAL_UINT32(300000, interval);  /* 5 minutes */
}

static void test_autotune_interval_high_temp(void)
{
    uint32_t interval = pm_get_autotune_interval_ms(70.0f);
    TEST_ASSERT_EQUAL_UINT32(500, interval);  /* 500ms */
}

static void test_clamp_float(void)
{
    TEST_ASSERT_FLOAT_WITHIN(FLOAT_DELTA, 50.0f, pm_clamp_f(30.0f, 50.0f, 100.0f));
    TEST_ASSERT_FLOAT_WITHIN(FLOAT_DELTA, 100.0f, pm_clamp_f(150.0f, 50.0f, 100.0f));
    TEST_ASSERT_FLOAT_WITHIN(FLOAT_DELTA, 75.0f, pm_clamp_f(75.0f, 50.0f, 100.0f));
}

static void test_clamp_u16(void)
{
    TEST_ASSERT_EQUAL_UINT16(300, pm_clamp_u16(200, 300, 800));
    TEST_ASSERT_EQUAL_UINT16(800, pm_clamp_u16(900, 300, 800));
    TEST_ASSERT_EQUAL_UINT16(500, pm_clamp_u16(500, 300, 800));
}

/* ============================================
 * Test Suite Runner
 * ============================================ */

void run_power_management_calc_tests(void)
{
    /* Fan Speed Tests */
    RUN_TEST(test_fan_speed_below_min_temp);
    RUN_TEST(test_fan_speed_at_min_temp);
    RUN_TEST(test_fan_speed_at_throttle_temp);
    RUN_TEST(test_fan_speed_above_throttle_temp);
    RUN_TEST(test_fan_speed_linear_interpolation);
    RUN_TEST(test_fan_speed_custom_params);

    /* Overheat Detection Tests */
    RUN_TEST(test_overheat_none);
    RUN_TEST(test_overheat_chip_only);
    RUN_TEST(test_overheat_vr_only);
    RUN_TEST(test_overheat_both);
    RUN_TEST(test_overheat_vr_zero_ignored);
    RUN_TEST(test_should_trigger_overheat_yes);
    RUN_TEST(test_should_trigger_overheat_no_safe_values);
    RUN_TEST(test_should_trigger_overheat_no_normal_temp);
    RUN_TEST(test_overheat_severity_soft);
    RUN_TEST(test_overheat_severity_hard);

    /* Autotune Tests */
    RUN_TEST(test_autotune_skip_invalid_temp);
    RUN_TEST(test_autotune_skip_zero_hashrate);
    RUN_TEST(test_autotune_skip_warmup);
    RUN_TEST(test_autotune_skip_timing);
    RUN_TEST(test_autotune_reset_preset_on_low_hashrate);
    RUN_TEST(test_autotune_increase_freq_when_cold);
    RUN_TEST(test_autotune_decrease_freq_when_hot);
    RUN_TEST(test_autotune_respects_freq_limits);

    /* Utility Tests */
    RUN_TEST(test_hashrate_low_detection);
    RUN_TEST(test_target_hashrate_calculation);
    RUN_TEST(test_efficiency_calculation);
    RUN_TEST(test_efficiency_zero_hashrate);
    RUN_TEST(test_autotune_interval_normal_temp);
    RUN_TEST(test_autotune_interval_high_temp);
    RUN_TEST(test_clamp_float);
    RUN_TEST(test_clamp_u16);
}
