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

    double timeout_ms = calculate_bm_timeout_ms(frequency, asic_count, small_cores, cores, version_size, timeout_percent);
    double expected_ms = 27.962;

    TEST_ASSERT_FLOAT_WITHIN(0.01, expected_ms, timeout_ms);
}

TEST_CASE("Check asic timeout 2x BM1370", "[common]")
{
    float frequency = 450.0; // MHz
    size_t asic_count = 2;
    size_t small_cores = 2040;
    size_t cores = 128;
    size_t version_size = 65536;
    float timeout_percent = 0.5;

    double timeout_ms = calculate_bm_timeout_ms(frequency, asic_count, small_cores, cores, version_size, timeout_percent);
    double expected_ms = 76354.974;

    TEST_ASSERT_FLOAT_WITHIN(0.01, expected_ms, timeout_ms);
}

TEST_CASE("Check invalid inputs return zero timeout", "[common]")
{
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, calculate_bm_timeout_ms(450.0f, 0, 2040, 128, 65536, 0.5f));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, calculate_bm_timeout_ms(450.0f, 1, 2040, 0, 65536, 0.5f));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, calculate_bm_timeout_ms(450.0f, 1, 64, 128, 65536, 0.5f));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, calculate_bm_timeout_ms(0.0f, 1, 2040, 128, 65536, 0.5f));
}

TEST_CASE("Check asic timeout 3x BM1370", "[common]")
{
    float frequency = 450.0; // MHz
    size_t asic_count = 3; // not power of 2 chain length
    size_t small_cores = 2040;
    size_t cores = 128;
    size_t version_size = 256;
    float timeout_percent = 0.5;

    double timeout_ms = calculate_bm_timeout_ms(frequency, asic_count, small_cores, cores, version_size, timeout_percent);
    double expected_ms = 149.131;

    TEST_ASSERT_FLOAT_WITHIN(0.01, expected_ms, timeout_ms);
}

TEST_CASE("Check max asic timeout 1x BM1370", "[common]")
{
    float frequency = 450.0; // MHz
    size_t asic_count = 1;
    size_t small_cores = 2040;
    size_t cores = 128;
    size_t version_size = 65536;
    float timeout_percent = 1.0;

    double timeout_ms = calculate_bm_timeout_ms(frequency, asic_count, small_cores, cores, version_size, timeout_percent);
    double expected_ms = 305419.897;

    TEST_ASSERT_FLOAT_WITHIN(0.01, expected_ms, timeout_ms);
}

TEST_CASE("Check fixed version clamps serial_versions to one full nonce pass", "[common]")
{
    double fixed_ms = calculate_bm_timeout_ms(450.0f, 1, 2040, 128, 1, 1.0f);
    double sixteen_ms = calculate_bm_timeout_ms(450.0f, 1, 2040, 128, 16, 1.0f);

    TEST_ASSERT_FLOAT_WITHIN(0.01, 74.565, fixed_ms);
    TEST_ASSERT_FLOAT_WITHIN(0.001, sixteen_ms, fixed_ms);
}