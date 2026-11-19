#include "unity.h"

#include "pll.h"
#include <math.h>

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

TEST_CASE("BM1370 ramp steps stay in the stock VCO range", "[pll]")
{
    const float frequencies[] = {756.25f, 768.75f, 781.25f};
    const uint8_t feedback[] = {242, 246, 250};
    for (unsigned i = 0; i < sizeof(frequencies) / sizeof(frequencies[0]); i++) {
        uint8_t fb, ref, p1, p2;
        float actual;
        TEST_ASSERT_TRUE(pll_get_parameters_bm1370(frequencies[i], &fb, &ref, &p1, &p2, &actual));
        TEST_ASSERT_EQUAL_UINT8(feedback[i], fb);
        TEST_ASSERT_EQUAL_UINT8(2, ref);
        TEST_ASSERT_EQUAL_UINT8(4, p1);
        TEST_ASSERT_EQUAL_UINT8(1, p2);
        TEST_ASSERT_FLOAT_WITHIN(0.001, frequencies[i], actual);
    }
    for (unsigned step = 8; step <= 240; step++) {
        uint8_t fb, ref, p1, p2;
        float actual;
        TEST_ASSERT_TRUE(pll_get_parameters_bm1370(step * 6.25f, &fb, &ref, &p1, &p2, &actual));
        float vco = FREQ_MULT * fb / ref;
        TEST_ASSERT_TRUE(vco >= 2000.0f && vco < 3200.0f);
        TEST_ASSERT_TRUE(ref != 1 || vco <= 3125.0f);
        TEST_ASSERT_TRUE(p1 > p2 && p1 <= 7 && p2 >= 1);
        TEST_ASSERT_TRUE(fabsf(actual - step * 6.25f) < 6.25f);
    }
}

TEST_CASE("BM1370 rejects frequencies without a supported PLL solution", "[pll]")
{
    const float invalid[] = {NAN, INFINITY, -1.0f, 0.0f, 2000.0f};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        uint8_t fb, ref, p1, p2;
        float actual;
        TEST_ASSERT_FALSE(pll_get_parameters_bm1370(invalid[i], &fb, &ref, &p1, &p2, &actual));
        TEST_ASSERT_EQUAL_UINT8(0, ref);
    }
}
