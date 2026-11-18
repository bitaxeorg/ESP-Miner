#include "unity.h"
#include "global_state.h"
#include "freertos/task.h"
#include "bm13xx_test_harness.h"
#include <math.h>

static unsigned calls;
static unsigned fail_call;
static float requested[4];

bool bm1370_test_set_frequency(GlobalState *state);

static void transition_delay(TickType_t ticks)
{
    (void)ticks;
}

#define vTaskDelay transition_delay
#define do_frequency_transition test_frequency_transition
#include "../frequency_transition_bmXX.c"
#undef do_frequency_transition
#undef vTaskDelay

static float frequency_setter(float frequency)
{
    requested[calls++] = frequency;
    return calls == fail_call ? NAN : frequency;
}

TEST_CASE("A failed PLL step stops the ramp and preserves its last frequency", "[pll]")
{
    GlobalState state = {0};
    state.POWER_MANAGEMENT_MODULE.actual_frequency = 50.0f;
    state.POWER_MANAGEMENT_MODULE.frequency_value = 75.0f;
    calls = 0;
    fail_call = 2;
    TEST_ASSERT_FALSE(test_frequency_transition(&state, frequency_setter));
    TEST_ASSERT_EQUAL_UINT(2, calls);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 56.25f, state.POWER_MANAGEMENT_MODULE.actual_frequency);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 62.5f, requested[1]);
}

TEST_CASE("A failed final fractional PLL step preserves its last frequency", "[pll]")
{
    GlobalState state = {0};
    state.POWER_MANAGEMENT_MODULE.actual_frequency = 50.0f;
    state.POWER_MANAGEMENT_MODULE.frequency_value = 58.0f;
    calls = 0;
    fail_call = 2;
    TEST_ASSERT_FALSE(test_frequency_transition(&state, frequency_setter));
    TEST_ASSERT_EQUAL_UINT(2, calls);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 56.25f, state.POWER_MANAGEMENT_MODULE.actual_frequency);
}

TEST_CASE("A failed small PLL change preserves its previous frequency", "[pll]")
{
    GlobalState state = {0};
    state.POWER_MANAGEMENT_MODULE.actual_frequency = 50.0f;
    state.POWER_MANAGEMENT_MODULE.frequency_value = 51.0f;
    calls = 0;
    fail_call = 1;
    TEST_ASSERT_FALSE(test_frequency_transition(&state, frequency_setter));
    TEST_ASSERT_EQUAL_UINT(1, calls);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 50.0f, state.POWER_MANAGEMENT_MODULE.actual_frequency);
}

TEST_CASE("An unsupported BM1370 endpoint is rejected before starting its ramp", "[pll]")
{
    GlobalState *state = bm13xx_harness_begin(&bm13xx_harness_drivers[2]);
    state->POWER_MANAGEMENT_MODULE.actual_frequency = 625.0f;
    state->POWER_MANAGEMENT_MODULE.frequency_value = 2000.0f;
    bm13xx_harness_clear_packets();
    TEST_ASSERT_FALSE(bm1370_test_set_frequency(state));
    TEST_ASSERT_EQUAL_UINT(0, bm13xx_harness_packet_count());
    TEST_ASSERT_FLOAT_WITHIN(0.001, 625.0f, state->POWER_MANAGEMENT_MODULE.actual_frequency);
    bm13xx_harness_end();
}
