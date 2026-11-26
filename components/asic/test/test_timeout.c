#include "unity.h"

#include "asic_common.h"

TEST_CASE("Check asic timeout 1x BM1397", "[common]")
{
    float frequency = 450.0; // MHz
    size_t asic_count = 1;
    size_t small_cores = 672;
    size_t cores = 168;
    size_t version_size = 4;
    float timeout_percent = 0.75;

    float timeout_ms = calculate_bm_timeout_ms(256, 256, frequency, asic_count, small_cores, cores, version_size, timeout_percent);
    float expected_ms = 27.962f;

    TEST_ASSERT_FLOAT_WITHIN(0.01f, expected_ms, timeout_ms);
}

TEST_CASE("Check asic timeout 2x BM1370", "[common]")
{
    float frequency = 450.0; // MHz
    size_t asic_count = 2;
    size_t small_cores = 2040;
    size_t cores = 128;
    size_t version_size = 65536;
    float timeout_percent = 0.5;

    float timeout_ms = calculate_bm_timeout_ms(256, 256, frequency, asic_count, small_cores, cores, version_size, timeout_percent);
    float expected_ms = 76354.974f;

    TEST_ASSERT_FLOAT_WITHIN(0.01f, expected_ms, timeout_ms);
}

TEST_CASE("Check invalid inputs return zero timeout", "[common]")
{
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, calculate_bm_timeout_ms(0, 256, 450.0f, 1, 2040, 128, 65536, 0.5f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, calculate_bm_timeout_ms(256, 0, 450.0f, 1, 2040, 128, 65536, 0.5f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, calculate_bm_timeout_ms(256, 256, 450.0f, 0, 2040, 128, 65536, 0.5f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, calculate_bm_timeout_ms(256, 256, 450.0f, 257, 2040, 128, 65536, 0.5f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, calculate_bm_timeout_ms(128, 0x10, 327.0f, 129, 6725, 432, 65536, 0.95f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, calculate_bm_timeout_ms(256, 256, 450.0f, 1, 2040, 0, 65536, 0.5f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, calculate_bm_timeout_ms(256, 256, 450.0f, 1, 64, 128, 65536, 0.5f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, calculate_bm_timeout_ms(256, 256, 0.0f, 1, 2040, 128, 65536, 0.5f));
}

TEST_CASE("Check asic timeout 3x BM1370", "[common]")
{
    float frequency = 450.0; // MHz
    size_t asic_count = 3; // not power of 2 chain length: floor(256 / 3) = 85
    size_t small_cores = 2040;
    size_t cores = 128;
    size_t version_size = 256;
    float timeout_percent = 0.5;

    float timeout_ms = calculate_bm_timeout_ms(256, 256, frequency, asic_count, small_cores, cores, version_size, timeout_percent);
    float expected_ms = 198.064f;

    TEST_ASSERT_FLOAT_WITHIN(0.01f, expected_ms, timeout_ms);
}

TEST_CASE("Check max asic timeout 1x BM1370", "[common]")
{
    float frequency = 450.0; // MHz
    size_t asic_count = 1;
    size_t small_cores = 2040;
    size_t cores = 128;
    size_t version_size = 65536;
    float timeout_percent = 1.0;

    float timeout_ms = calculate_bm_timeout_ms(256, 256, frequency, asic_count, small_cores, cores, version_size, timeout_percent);
    float expected_ms = 305419.897f;

    TEST_ASSERT_FLOAT_WITHIN(0.01f, expected_ms, timeout_ms);
}

TEST_CASE("Check fixed version clamps serial_versions to one full nonce pass", "[common]")
{
    float fixed_ms = calculate_bm_timeout_ms(256, 256, 450.0f, 1, 2040, 128, 1, 1.0f);
    float sixteen_ms = calculate_bm_timeout_ms(256, 256, 450.0f, 1, 2040, 128, 16, 1.0f);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 74.565f, fixed_ms);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, sixteen_ms, fixed_ms);
}

TEST_CASE("Check asic timeout 6x BM1370", "[common]")
{
    float timeout_ms = calculate_bm_timeout_ms(256, 256, 525.0f, 6, 2040, 128, 65536, 0.95f);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 40802.19f, timeout_ms);
}

TEST_CASE("Check asic timeout BM1373 across chip counts", "[common]")
{
    // For 1..8 chips, BM1373 clamps stride to 0x10 (16 / 128 = 1/8 of space, 8 buckets).
    float rolled_2x_ms = calculate_bm_timeout_ms(128, 0x10, 327.0f, 2, 6725, 432, 65536, 0.95f);
    float rolled_8x_ms = calculate_bm_timeout_ms(128, 0x10, 327.0f, 8, 6725, 432, 65536, 0.95f);
    float rolled_16x_ms = calculate_bm_timeout_ms(128, 0x10, 327.0f, 16, 6725, 432, 65536, 0.95f);
    float fixed_2x_ms = calculate_bm_timeout_ms(128, 0x10, 327.0f, 2, 6725, 432, 1, 0.95f);

    TEST_ASSERT_FLOAT_WITHIN(1.0f, 12477.7f, rolled_2x_ms);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, rolled_2x_ms, rolled_8x_ms);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, rolled_2x_ms * 0.5f, rolled_16x_ms);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.046f, fixed_2x_ms);
}

TEST_CASE("Check HCN-derived timeout tracks 25 MHz crystal ticks directly", "[common]")
{
    // Issue #514 Supra (BM1368: 80 cores -> 128, 1276 small cores -> 2048 -> 16/core, 4096 batches):
    // Fixed HCN 0x15A4 (5540) -> 1815.35 ms nominal; with +136 overrun -> 1859.92 ms regardless of PLL MHz.
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 1815.35f, calculate_bm_hcn_timeout_ms(0x15A4, 1276, 80, 65536, 1.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 1859.92f, calculate_bm_hcn_timeout_ms(0x15A4 + 136, 1276, 80, 65536, 1.0f));

    // Naja Duo (BM1373 @ 327 MHz: HCN = 40083 - 268 = 39815 = 0x9B87, 4096 batches, 0.95f):
    uint32_t naja_hcn = calculate_bm_hcn(128, 0x10, 1.0, 327.0f, 2, 432);
    TEST_ASSERT_EQUAL_HEX32(0x00009B87, naja_hcn);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 12394.25f, calculate_bm_hcn_timeout_ms(naja_hcn, 6725, 432, 65536, 0.95f));

    // Gamma Hex (6x BM1370 @ 525 MHz: HCN = 131072 - 268 = 130804, 4096 batches, 0.95f):
    uint32_t hex_hcn = calculate_bm_hcn(256, 256, 1.0, 525.0f, 6, 128);
    TEST_ASSERT_EQUAL_UINT32(130804, hex_hcn);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 40718.68f, calculate_bm_hcn_timeout_ms(hex_hcn, 2040, 128, 65536, 0.95f));

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, calculate_bm_hcn_timeout_ms(0, 2040, 128, 65536, 0.95f));
}