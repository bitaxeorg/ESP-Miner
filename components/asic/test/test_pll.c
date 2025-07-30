#include "unity.h"
#include "pll.h"

TEST_CASE("PLL frequency calculation for 60-200 range, 300 MHz", "[pll]")
{
    float frequency = 300.0;
    uint8_t fb_divider, refdiv, postdiv1, postdiv2;
    float actual_freq;

    pll_get_parameters(frequency, 60, 200, &fb_divider, &refdiv, &postdiv1, &postdiv2, &actual_freq);

    TEST_ASSERT_EQUAL_UINT8(69, fb_divider);
    TEST_ASSERT_EQUAL_UINT8(2, refdiv);
    TEST_ASSERT_EQUAL_UINT8(3, postdiv1);
    TEST_ASSERT_EQUAL_UINT8(2, postdiv2);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 299.0, actual_freq);
}

TEST_CASE("PLL frequency calculation for 60-200 range, low boundary 60", "[pll]")
{
    float frequency = 260.0;
    uint8_t fb_divider, refdiv, postdiv1, postdiv2;
    float actual_freq;

    pll_get_parameters(frequency, 60, 200, &fb_divider, &refdiv, &postdiv1, &postdiv2, &actual_freq);

    TEST_ASSERT_EQUAL_UINT8(60, fb_divider);
    TEST_ASSERT_EQUAL_UINT8(2, refdiv);
    TEST_ASSERT_EQUAL_UINT8(3, postdiv1);
    TEST_ASSERT_EQUAL_UINT8(2, postdiv2);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 260.0, actual_freq);
}

TEST_CASE("PLL frequency calculation for 144-235 range, 500 MHz", "[pll]")
{
    float frequency = 500.0;
    uint8_t fb_divider, refdiv, postdiv1, postdiv2;
    float actual_freq;

    pll_get_parameters(frequency, 144, 235, &fb_divider, &refdiv, &postdiv1, &postdiv2, &actual_freq);

    TEST_ASSERT_EQUAL_UINT8(154, fb_divider);
    TEST_ASSERT_EQUAL_UINT8(2, refdiv);
    TEST_ASSERT_EQUAL_UINT8(2, postdiv1);
    TEST_ASSERT_EQUAL_UINT8(2, postdiv2);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 500.5, actual_freq);
}

TEST_CASE("PLL frequency calculation for 144-235 range, high boundary 235", "[pll]")
{
    float frequency = 1018.0;
    uint8_t fb_divider, refdiv, postdiv1, postdiv2;
    float actual_freq;

    pll_get_parameters(frequency, 144, 235, &fb_divider, &refdiv, &postdiv1, &postdiv2, &actual_freq);

    TEST_ASSERT_EQUAL_UINT8(235, fb_divider);
    TEST_ASSERT_EQUAL_UINT8(2, refdiv);
    TEST_ASSERT_EQUAL_UINT8(3, postdiv1);
    TEST_ASSERT_EQUAL_UINT8(2, postdiv2);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 1018.333, actual_freq);
}

TEST_CASE("PLL frequency calculation for 160-239 range, 600 MHz", "[pll]")
{
    float frequency = 600.0;
    uint8_t fb_divider, refdiv, postdiv1, postdiv2;
    float actual_freq;

    pll_get_parameters(frequency, 160, 239, &fb_divider, &refdiv, &postdiv1, &postdiv2, &actual_freq);

    TEST_ASSERT_EQUAL_UINT8(184, fb_divider);
    TEST_ASSERT_EQUAL_UINT8(2, refdiv);
    TEST_ASSERT_EQUAL_UINT8(2, postdiv1);
    TEST_ASSERT_EQUAL_UINT8(2, postdiv2);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 598.0, actual_freq);
}

TEST_CASE("PLL frequency calculation for 160-239 range, non-exact 750.5 MHz", "[pll]")
{
    float frequency = 750.5;
    uint8_t fb_divider, refdiv, postdiv1, postdiv2;
    float actual_freq;

    pll_get_parameters(frequency, 160, 239, &fb_divider, &refdiv, &postdiv1, &postdiv2, &actual_freq);

    TEST_ASSERT_EQUAL_UINT8(173, fb_divider);
    TEST_ASSERT_EQUAL_UINT8(2, refdiv);
    TEST_ASSERT_EQUAL_UINT8(1, postdiv1);
    TEST_ASSERT_EQUAL_UINT8(1, postdiv2);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 750.5, actual_freq);
}

TEST_CASE("PLL frequency calculation for 160-239 range, low boundary 160", "[pll]")
{
    float frequency = 520.0;
    uint8_t fb_divider, refdiv, postdiv1, postdiv2;
    float actual_freq;

    pll_get_parameters(frequency, 160, 239, &fb_divider, &refdiv, &postdiv1, &postdiv2, &actual_freq);

    TEST_ASSERT_EQUAL_UINT8(160, fb_divider);
    TEST_ASSERT_EQUAL_UINT8(2, refdiv);
    TEST_ASSERT_EQUAL_UINT8(2, postdiv1);
    TEST_ASSERT_EQUAL_UINT8(2, postdiv2);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 520.0, actual_freq);
}
