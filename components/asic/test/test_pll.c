#include "unity.h"

#include "pll.h"

TEST_CASE("Check PLL frequency calculation", "[pll]")
{
    float frequency = 450.0; // MHz
    uint8_t fb_divider, refdiv, postdiv1, postdiv2;
    float actual_freq;

    pll_get_parameters(frequency, 60, 200, &fb_divider, &refdiv, &postdiv1, &postdiv2, &actual_freq);

    TEST_ASSERT_EQUAL_UINT8(72, fb_divider);
    TEST_ASSERT_EQUAL_UINT8(2, refdiv);
    TEST_ASSERT_EQUAL_UINT8(2, postdiv1);
    TEST_ASSERT_EQUAL_UINT8(1, postdiv2);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 450.0, actual_freq);
}
