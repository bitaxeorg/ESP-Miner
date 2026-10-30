#include "unity.h"
#include "device_config.h"
#include <stdint.h>

#define TEST_STUB_GLOBAL_STATE_H
#define GLOBAL_STATE_H_
typedef struct GlobalState {
    DeviceConfig DEVICE_CONFIG;
    struct { float expected_hashrate, frequency_value; } POWER_MANAGEMENT_MODULE;
    struct { double pool_difficulty; } SYSTEM_MODULE;
    struct { bool is_active; } SELF_TEST_MODULE;
    bool ASIC_initalized;
    uint8_t current_difficulty_power;
    double target_share_interval_s;
} GlobalState;

#define difficulty_controller_update controller_test_update
#define ASIC_set_difficulty controller_apply
#define esp_timer_get_time controller_time
#include "../../../main/difficulty_controller.c"

static int64_t now_us;
static uint8_t applied_power;
static unsigned apply_count;
int64_t controller_time(void) { return now_us; }
void controller_apply(GlobalState *state, uint8_t power)
{
    TEST_ASSERT_EQUAL_UINT8(power, state->current_difficulty_power);
    applied_power = power;
    ++apply_count;
}

static GlobalState controller_fixture(AsicConfig asic)
{
    s_pending_difficulty_power = 0;
    s_pending_increase_time_us = 0;
    now_us = 1000000;
    applied_power = apply_count = 0;
    GlobalState state = {.ASIC_initalized = true};
    state.DEVICE_CONFIG.family = FAMILY_BONANZA;
    state.DEVICE_CONFIG.family.asic = asic;
    state.POWER_MANAGEMENT_MODULE.expected_hashrate = 1007;
    state.SYSTEM_MODULE.pool_difficulty = 4096;
    return state;
}

TEST_CASE("BZM uses the shared controller immediately with pool caps and increase grace", "[asic][difficulty]")
{
    GlobalState state = controller_fixture(ASIC_BZM);
    difficulty_controller_update(NULL);
    state.ASIC_initalized = false;
    difficulty_controller_update(&state);
    TEST_ASSERT_EQUAL_UINT32(0, apply_count);
    state.ASIC_initalized = true;
    difficulty_controller_update(&state);
    TEST_ASSERT_EQUAL_UINT8(9, applied_power); // D512, default two-second interval.
    TEST_ASSERT_EQUAL_UINT32(1, apply_count);
    difficulty_controller_update(&state);
    TEST_ASSERT_EQUAL_UINT32(1, apply_count);
    state.SYSTEM_MODULE.pool_difficulty = 256;
    difficulty_controller_update(&state);
    TEST_ASSERT_EQUAL_UINT8(8, applied_power); // Pool cap lowers immediately.
    state.SYSTEM_MODULE.pool_difficulty = 4096;
    difficulty_controller_update(&state);
    TEST_ASSERT_EQUAL_UINT8(8, applied_power);
    now_us += 29999999;
    difficulty_controller_update(&state);
    TEST_ASSERT_EQUAL_UINT32(2, apply_count);
    ++now_us;
    difficulty_controller_update(&state);
    TEST_ASSERT_EQUAL_UINT8(9, applied_power);
    TEST_ASSERT_EQUAL_UINT32(3, apply_count);
    state.current_difficulty_power = 0; // Resume must not wait for a fresh grace period.
    difficulty_controller_update(&state);
    TEST_ASSERT_EQUAL_UINT32(4, apply_count);
}

TEST_CASE("Max keeps the same controller policy", "[asic][difficulty]")
{
    GlobalState state = controller_fixture(ASIC_BM1397);
    state.POWER_MANAGEMENT_MODULE.expected_hashrate = 500;
    difficulty_controller_update(&state);
    TEST_ASSERT_EQUAL_UINT8(8, applied_power);
    state.SYSTEM_MODULE.pool_difficulty = 32;
    difficulty_controller_update(&state);
    TEST_ASSERT_EQUAL_UINT8(5, applied_power);
    TEST_ASSERT_EQUAL_UINT32(2, apply_count);
}

TEST_CASE("BZM fallback difficulty includes configured throughput at saved and default frequencies", "[asic][difficulty]")
{
    GlobalState state = controller_fixture(ASIC_BZM);
    state.POWER_MANAGEMENT_MODULE.expected_hashrate = 0;
    state.POWER_MANAGEMENT_MODULE.frequency_value = 1400;
    difficulty_controller_update(&state);
    TEST_ASSERT_EQUAL_UINT8(10, applied_power);

    state = controller_fixture(ASIC_BZM);
    state.POWER_MANAGEMENT_MODULE.expected_hashrate = 0;
    difficulty_controller_update(&state);
    TEST_ASSERT_EQUAL_UINT8(9, applied_power);
}
