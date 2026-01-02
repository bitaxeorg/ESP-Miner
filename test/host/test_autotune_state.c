/**
 * @file test_autotune_state.c
 * @brief Unit tests for autotune_state module
 *
 * Tests the thread-safe autotune state management wrapper.
 */

#include "unity.h"
#include "autotune_state.h"
#include <stdlib.h>

/* Test group runner */
void run_autotune_state_tests(void);

/* ============================================
 * Lifecycle Tests
 * ============================================ */

static void test_autotune_state_create_returns_valid_handle(void)
{
    autotune_state_t state = autotune_state_create();
    TEST_ASSERT_NOT_NULL(state);
    TEST_ASSERT_TRUE(autotune_state_is_valid(state));
    autotune_state_destroy(state);
}

static void test_autotune_state_destroy_handles_null(void)
{
    /* Should not crash */
    autotune_state_destroy(NULL);
}

static void test_autotune_state_is_valid_returns_false_for_null(void)
{
    TEST_ASSERT_FALSE(autotune_state_is_valid(NULL));
}

static void test_autotune_state_reset_clears_all_state(void)
{
    autotune_state_t state = autotune_state_create();
    TEST_ASSERT_NOT_NULL(state);

    /* Set some state */
    autotune_state_update_last_adjust_time(state, 5000);
    autotune_state_increment_low_hashrate(state);
    autotune_state_increment_low_hashrate(state);

    /* Verify state was set */
    TEST_ASSERT_EQUAL_UINT8(2, autotune_state_get_low_hashrate_count(state));

    /* Reset and verify */
    autotune_state_reset(state);
    TEST_ASSERT_EQUAL_UINT8(0, autotune_state_get_low_hashrate_count(state));

    /* After reset, ms_since should be current time (since last_adjust is 0) */
    uint32_t ms_since = autotune_state_get_ms_since_last_adjust(state, 1000);
    TEST_ASSERT_EQUAL_UINT32(1000, ms_since);

    autotune_state_destroy(state);
}

/* ============================================
 * Timing Tests
 * ============================================ */

static void test_autotune_state_timing_initial_state(void)
{
    autotune_state_t state = autotune_state_create();
    TEST_ASSERT_NOT_NULL(state);

    /* Initial state should have last_adjust_time = 0 */
    uint32_t ms_since = autotune_state_get_ms_since_last_adjust(state, 1000);
    TEST_ASSERT_EQUAL_UINT32(1000, ms_since);

    autotune_state_destroy(state);
}

static void test_autotune_state_timing_after_update(void)
{
    autotune_state_t state = autotune_state_create();
    TEST_ASSERT_NOT_NULL(state);

    /* Set last adjust time to 1000ms */
    autotune_state_update_last_adjust_time(state, 1000);

    /* Check elapsed time at 1500ms */
    uint32_t ms_since = autotune_state_get_ms_since_last_adjust(state, 1500);
    TEST_ASSERT_EQUAL_UINT32(500, ms_since);

    autotune_state_destroy(state);
}

static void test_autotune_state_timing_wraparound(void)
{
    autotune_state_t state = autotune_state_create();
    TEST_ASSERT_NOT_NULL(state);

    /* Set last adjust time near max uint32 */
    autotune_state_update_last_adjust_time(state, 0xFFFFFF00);

    /* Current time after wraparound */
    uint32_t current = 0x00000100; /* 256 after wraparound */

    uint32_t ms_since = autotune_state_get_ms_since_last_adjust(state, current);
    /* 0x100 - 0xFFFFFF00 = 0x200 (512) due to unsigned wraparound */
    TEST_ASSERT_EQUAL_UINT32(0x200, ms_since);

    autotune_state_destroy(state);
}

/* ============================================
 * Low Hashrate Counter Tests
 * ============================================ */

static void test_autotune_state_low_hashrate_initial_zero(void)
{
    autotune_state_t state = autotune_state_create();
    TEST_ASSERT_NOT_NULL(state);

    TEST_ASSERT_EQUAL_UINT8(0, autotune_state_get_low_hashrate_count(state));

    autotune_state_destroy(state);
}

static void test_autotune_state_low_hashrate_increment(void)
{
    autotune_state_t state = autotune_state_create();
    TEST_ASSERT_NOT_NULL(state);

    uint8_t count1 = autotune_state_increment_low_hashrate(state);
    TEST_ASSERT_EQUAL_UINT8(1, count1);

    uint8_t count2 = autotune_state_increment_low_hashrate(state);
    TEST_ASSERT_EQUAL_UINT8(2, count2);

    uint8_t count3 = autotune_state_increment_low_hashrate(state);
    TEST_ASSERT_EQUAL_UINT8(3, count3);

    TEST_ASSERT_EQUAL_UINT8(3, autotune_state_get_low_hashrate_count(state));

    autotune_state_destroy(state);
}

static void test_autotune_state_low_hashrate_reset(void)
{
    autotune_state_t state = autotune_state_create();
    TEST_ASSERT_NOT_NULL(state);

    /* Increment a few times */
    autotune_state_increment_low_hashrate(state);
    autotune_state_increment_low_hashrate(state);
    autotune_state_increment_low_hashrate(state);
    TEST_ASSERT_EQUAL_UINT8(3, autotune_state_get_low_hashrate_count(state));

    /* Reset */
    autotune_state_reset_low_hashrate(state);
    TEST_ASSERT_EQUAL_UINT8(0, autotune_state_get_low_hashrate_count(state));

    autotune_state_destroy(state);
}

static void test_autotune_state_low_hashrate_no_overflow(void)
{
    autotune_state_t state = autotune_state_create();
    TEST_ASSERT_NOT_NULL(state);

    /* Increment 256 times - should saturate at 255 */
    for (int i = 0; i < 260; i++) {
        autotune_state_increment_low_hashrate(state);
    }

    /* Should be capped at 255 */
    TEST_ASSERT_EQUAL_UINT8(255, autotune_state_get_low_hashrate_count(state));

    autotune_state_destroy(state);
}

/* ============================================
 * Null Handle Safety Tests
 * ============================================ */

static void test_autotune_state_null_handle_get_ms(void)
{
    uint32_t result = autotune_state_get_ms_since_last_adjust(NULL, 1000);
    TEST_ASSERT_EQUAL_UINT32(0, result);
}

static void test_autotune_state_null_handle_update_time(void)
{
    /* Should not crash */
    autotune_state_update_last_adjust_time(NULL, 1000);
}

static void test_autotune_state_null_handle_get_count(void)
{
    uint8_t result = autotune_state_get_low_hashrate_count(NULL);
    TEST_ASSERT_EQUAL_UINT8(0, result);
}

static void test_autotune_state_null_handle_increment(void)
{
    uint8_t result = autotune_state_increment_low_hashrate(NULL);
    TEST_ASSERT_EQUAL_UINT8(0, result);
}

static void test_autotune_state_null_handle_reset_hashrate(void)
{
    /* Should not crash */
    autotune_state_reset_low_hashrate(NULL);
}

static void test_autotune_state_null_handle_reset(void)
{
    /* Should not crash */
    autotune_state_reset(NULL);
}

/* ============================================
 * Test Runner
 * ============================================ */

void run_autotune_state_tests(void)
{
    /* Lifecycle tests */
    RUN_TEST(test_autotune_state_create_returns_valid_handle);
    RUN_TEST(test_autotune_state_destroy_handles_null);
    RUN_TEST(test_autotune_state_is_valid_returns_false_for_null);
    RUN_TEST(test_autotune_state_reset_clears_all_state);

    /* Timing tests */
    RUN_TEST(test_autotune_state_timing_initial_state);
    RUN_TEST(test_autotune_state_timing_after_update);
    RUN_TEST(test_autotune_state_timing_wraparound);

    /* Low hashrate counter tests */
    RUN_TEST(test_autotune_state_low_hashrate_initial_zero);
    RUN_TEST(test_autotune_state_low_hashrate_increment);
    RUN_TEST(test_autotune_state_low_hashrate_reset);
    RUN_TEST(test_autotune_state_low_hashrate_no_overflow);

    /* Null handle safety tests */
    RUN_TEST(test_autotune_state_null_handle_get_ms);
    RUN_TEST(test_autotune_state_null_handle_update_time);
    RUN_TEST(test_autotune_state_null_handle_get_count);
    RUN_TEST(test_autotune_state_null_handle_increment);
    RUN_TEST(test_autotune_state_null_handle_reset_hashrate);
    RUN_TEST(test_autotune_state_null_handle_reset);
}
