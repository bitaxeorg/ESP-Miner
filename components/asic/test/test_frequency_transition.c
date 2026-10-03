#include "unity.h"
#include "global_state.h"
#include "frequency_transition_bmXX.h"

#include <float.h>
#include <math.h>
#include <string.h>

static void transition_delay(TickType_t ticks)
{
    (void)ticks;
}

#define do_frequency_transition test_frequency_transition
#define vTaskDelay transition_delay
#include "../frequency_transition_bmXX.c"
#undef vTaskDelay
#undef do_frequency_transition

static unsigned writes;
static float reject_at;
static float failure_value;

static float test_set_frequency(float frequency)
{
    writes++;
    return frequency >= reject_at ? failure_value : frequency;
}

static void check_transition(float target, float fail_at, float failed_result,
                             float expected_actual, unsigned expected_writes)
{
    static GlobalState state;
    memset(&state, 0, sizeof(state));
    state.POWER_MANAGEMENT_MODULE.actual_frequency = 50;
    state.POWER_MANAGEMENT_MODULE.frequency_value = target;
    writes = 0;
    reject_at = fail_at;
    failure_value = failed_result;
    test_frequency_transition(&state, test_set_frequency);
    TEST_ASSERT_EQUAL_FLOAT(expected_actual, state.POWER_MANAGEMENT_MODULE.actual_frequency);
    TEST_ASSERT_EQUAL_UINT32(expected_writes, writes);
}

TEST_CASE("frequency ramp retains the last valid clock on rejected PLL", "[asic][pll]")
{
    check_transition(51, 51, 0, 50, 1);       // Short transition.
    check_transition(75, 62.5f, 0, 56.25f, 2); // Intermediate step fails.
    check_transition(63, 63, 0, 62.5f, 3);   // Final non-step target fails.
    check_transition(51, 51, NAN, 50, 1);
    check_transition(75, INFINITY, 0, 75, 4);
}

TEST_CASE("frequency ramp rejects nonfinite and out of range inputs", "[asic][pll]")
{
    const float invalid[] = {0, -1, NAN, INFINITY, FLT_MAX};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        check_transition(invalid[i], INFINITY, 0, 50, 0);
    }
}
