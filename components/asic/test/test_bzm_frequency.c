#include "unity.h"

#include <math.h>

#include "bzm/frequency.h"

TEST_CASE("BZM frequency targets use the fixed BIRDS PLL math",
          "[asic][bzm][frequency]")
{
    bzm_frequency_target_t target;

    TEST_ASSERT_TRUE(bzm_frequency_resolve_target(800.0f, &target));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 800.0f, target.actual_mhz);
    TEST_ASSERT_EQUAL_UINT16(128, target.feedback_divider);
    TEST_ASSERT_EQUAL_HEX32(0x1242, target.postdiv_register);

    TEST_ASSERT_TRUE(bzm_frequency_resolve_target(1200.0f, &target));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1200.0f, target.actual_mhz);
    TEST_ASSERT_EQUAL_UINT16(192, target.feedback_divider);

    TEST_ASSERT_TRUE(bzm_frequency_resolve_target(1312.5f, &target));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1312.5f, target.actual_mhz);
    TEST_ASSERT_EQUAL_UINT16(210, target.feedback_divider);

    TEST_ASSERT_TRUE(bzm_frequency_resolve_target(1203.125f, &target));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1200.0f, target.actual_mhz);
    TEST_ASSERT_EQUAL_UINT16(192, target.feedback_divider);
}

TEST_CASE("BZM frequency targeting clamps to its range and rejects NaN",
          "[asic][bzm][frequency]")
{
    bzm_frequency_target_t target;

    TEST_ASSERT_FALSE(bzm_frequency_request_is_valid(799.0f));
    TEST_ASSERT_FALSE(bzm_frequency_request_is_valid(2001.0f));
    TEST_ASSERT_TRUE(bzm_frequency_request_is_valid(800.0f));
    TEST_ASSERT_TRUE(bzm_frequency_request_is_valid(2000.0f));

    TEST_ASSERT_TRUE(bzm_frequency_resolve_target(700.0f, &target));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 800.0f, target.actual_mhz);
    TEST_ASSERT_TRUE(bzm_frequency_resolve_target(2100.0f, &target));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2000.0f, target.actual_mhz);
    TEST_ASSERT_FALSE(bzm_frequency_resolve_target(NAN, &target));
}

TEST_CASE("BZM startup shortcut is target minus 100 MHz, capped at 1425",
          "[asic][bzm][frequency]")
{
    TEST_ASSERT_FLOAT_WITHIN(
        0.001f, 800.0f, bzm_frequency_initial_mhz(850.0f));
    TEST_ASSERT_FLOAT_WITHIN(
        0.001f, 1100.0f, bzm_frequency_initial_mhz(1200.0f));
    TEST_ASSERT_FLOAT_WITHIN(
        0.001f, 1400.0f, bzm_frequency_initial_mhz(1500.0f));
    TEST_ASSERT_FLOAT_WITHIN(
        0.001f, 1425.0f, bzm_frequency_initial_mhz(2000.0f));

}
