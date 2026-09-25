#include "bm13xx_test_bindings.h"
#include "bm13xx_test_harness.h"
#include "bm1397_test_harness.h"
#include "bm1366.h"
#include "bm1368.h"
#include "bm1370.h"
#include "bm1373.h"
#include "unity.h"

#include <float.h>
#include <math.h>

// The isolated BM1397 instance uses its own UART spy and symbol namespace.
float bm1397_test_send_hash_frequency(float target_freq);

TEST_CASE("BM13xx invalid PLL targets do not write ASIC registers", "[asic][pll]")
{
    float (*const setters[])(float) = {
        BM1366_send_hash_frequency, BM1368_send_hash_frequency,
        BM1370_send_hash_frequency, BM1373_send_hash_frequency,
    };
    const float invalid[] = {1, 0, -1, 65535, FLT_MAX, NAN, INFINITY};
    for (size_t d = 0; d < sizeof(setters) / sizeof(setters[0]); ++d) {
        bm13xx_harness_begin();
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
            TEST_ASSERT_EQUAL_FLOAT(0, setters[d](invalid[i]));
            TEST_ASSERT_EQUAL_UINT32(0, bm13xx_harness_packet_count());
        }
        TEST_ASSERT_FLOAT_WITHIN(0.01f, 525, setters[d](525));
        TEST_ASSERT_GREATER_THAN_UINT32(0, bm13xx_harness_packet_count());
        bm13xx_harness_end();
    }
}

TEST_CASE("BM1397 invalid PLL targets do not write ASIC registers", "[asic][pll]")
{
    const float invalid[] = {1, 0, -1, 65535, FLT_MAX, NAN, INFINITY};
    bm1397_harness_begin();
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        TEST_ASSERT_EQUAL_FLOAT(0, bm1397_test_send_hash_frequency(invalid[i]));
        TEST_ASSERT_EQUAL_UINT32(0, bm1397_harness_packet_count());
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 450, bm1397_test_send_hash_frequency(450));
    TEST_ASSERT_GREATER_THAN_UINT32(0, bm1397_harness_packet_count());
    bm1397_harness_end();
}
