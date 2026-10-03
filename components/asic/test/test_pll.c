#include "unity.h"

#include "pll.h"
#include <float.h>
#include <math.h>
#include "../../../main/power/frequency_limits.h"

TEST_CASE("Check PLL frequency calculation", "[pll]")
{
    float frequency = 450.0; // MHz
    uint8_t fb_divider, refdiv, postdiv1, postdiv2;
    float actual_freq;

    TEST_ASSERT_TRUE(pll_get_parameters(frequency, 60, 200, &fb_divider, &refdiv, &postdiv1, &postdiv2, &actual_freq));

    TEST_ASSERT_EQUAL_UINT8(72, fb_divider);
    TEST_ASSERT_EQUAL_UINT8(2, refdiv);
    TEST_ASSERT_EQUAL_UINT8(2, postdiv1);
    TEST_ASSERT_EQUAL_UINT8(1, postdiv2);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 450.0, actual_freq);
}

TEST_CASE("PLL rejects invalid targets without modifying outputs", "[pll]")
{
    const float invalid[] = {0, -1, 1, 25, 47, 65535, FLT_MAX, INFINITY, -INFINITY, NAN};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        uint8_t fb = 168, ref = 2, p1 = 4, p2 = 1;
        float actual = 525;
        TEST_ASSERT_FALSE(pll_get_parameters(invalid[i], 160, 239, &fb, &ref, &p1, &p2, &actual));
        TEST_ASSERT_EQUAL_UINT8(168, fb);
        TEST_ASSERT_EQUAL_UINT8(2, ref);
        TEST_ASSERT_EQUAL_UINT8(4, p1);
        TEST_ASSERT_EQUAL_UINT8(1, p2);
        TEST_ASSERT_EQUAL_FLOAT(525, actual);
    }
}

TEST_CASE("PLL keeps valid family settings representable", "[pll]")
{
    const uint16_t bounds[][2] = {{60, 200}, {144, 235}, {160, 239}};
    const float targets[] = {50, 100, 125, 327, 425, 450, 525, 690, 1000};
    for (size_t b = 0; b < sizeof(bounds) / sizeof(bounds[0]); ++b) {
        for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i) {
            uint8_t fb, ref, p1, p2;
            float actual;
            TEST_ASSERT_TRUE(pll_get_parameters(targets[i], bounds[b][0], bounds[b][1], &fb, &ref, &p1, &p2, &actual));
            TEST_ASSERT_TRUE(fb >= bounds[b][0] && fb <= bounds[b][1]);
            TEST_ASSERT_TRUE(ref >= 1 && ref <= 2 && p2 >= 1 && p1 > p2 && p1 <= 7);
            TEST_ASSERT_FLOAT_WITHIN(0.001f, FREQ_MULT * fb / (ref * p1 * p2), actual);
            TEST_ASSERT_FLOAT_WITHIN(1.0f, targets[i], actual);
        }
    }
}

TEST_CASE("overheat recovery strictly reduces frequency above the floor", "[pll][power]")
{
    for (unsigned quarter = 201; quarter <= 4800; ++quarter) {
        float current = quarter / 4.0f;
        float next = -1;
        TEST_ASSERT_TRUE(asic_recovery_frequency(current, 100, &next));
        TEST_ASSERT_TRUE(next < current);
        TEST_ASSERT_TRUE(next >= ASIC_MIN_FREQUENCY_MHZ);
    }
    const float before[] = {100, 100.25f, 125, 150, 525};
    const float expected[] = {50, 50, 50, 50, 425};
    for (size_t i = 0; i < sizeof(before) / sizeof(before[0]); ++i) {
        float next;
        TEST_ASSERT_TRUE(asic_recovery_frequency(before[i], 100, &next));
        TEST_ASSERT_EQUAL_FLOAT(expected[i], next);
    }
}

TEST_CASE("overheat at the minimum frequency cannot restart at a higher clock", "[pll][power]")
{
    const float invalid[] = {0, -1, 25, 50, NAN, INFINITY};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        float next = -1;
        TEST_ASSERT_FALSE(asic_recovery_frequency(invalid[i], 100, &next));
        TEST_ASSERT_EQUAL_FLOAT(-1, next);
    }
    float next = -1;
    TEST_ASSERT_FALSE(asic_recovery_frequency(525, 0, &next));
    TEST_ASSERT_FALSE(asic_recovery_frequency(525, NAN, &next));
    TEST_ASSERT_FALSE(asic_recovery_frequency(525, 100, NULL));
}
