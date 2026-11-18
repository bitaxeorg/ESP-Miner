#include "unity.h"
#include "stratum_timing.h"
#include <string.h>

TEST_CASE("stratum_timing record and calculate latency", "[stratum_timing]")
{
    stratum_timing_tracker_t tracker;
    stratum_timing_reset(&tracker);

    // Record request 1 at t = 1,000,000 us (1.0s)
    stratum_timing_record(&tracker, 1, 1000000ULL);

    // Calculate response at t = 1,025,000 us (25ms later)
    float latency_ms = stratum_timing_calculate_ms(&tracker, 1, 1025000ULL);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, latency_ms);

    // Second call should return -1.0f as the slot is cleared
    latency_ms = stratum_timing_calculate_ms(&tracker, 1, 1030000ULL);
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, latency_ms);
}

TEST_CASE("stratum_timing wraparound and slot reuse", "[stratum_timing]")
{
    stratum_timing_tracker_t tracker;
    stratum_timing_reset(&tracker);

    uint32_t id_first = 5;
    uint32_t id_second = 5 + STRATUM_TIMING_SLOTS; // Wraps around to the exact same slot

    // Record first ID
    stratum_timing_record(&tracker, id_first, 2000000ULL);

    // Second ID wraps around and overwrites the slot
    stratum_timing_record(&tracker, id_second, 2100000ULL);

    // Calculate for the second ID
    float latency_ms = stratum_timing_calculate_ms(&tracker, id_second, 2115500ULL);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.5f, latency_ms);

    // Slot is now cleared, checking first or second ID yields -1.0f
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, stratum_timing_calculate_ms(&tracker, id_first, 2120000ULL));
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, stratum_timing_calculate_ms(&tracker, id_second, 2120000ULL));
}

TEST_CASE("stratum_timing reset", "[stratum_timing]")
{
    stratum_timing_tracker_t tracker;
    stratum_timing_reset(&tracker);

    stratum_timing_record(&tracker, 10, 5000000ULL);
    stratum_timing_record(&tracker, 11, 5000100ULL);
    stratum_timing_record(&tracker, 12, 5000200ULL);

    stratum_timing_reset(&tracker);

    TEST_ASSERT_EQUAL_FLOAT(-1.0f, stratum_timing_calculate_ms(&tracker, 10, 5050000ULL));
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, stratum_timing_calculate_ms(&tracker, 11, 5050000ULL));
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, stratum_timing_calculate_ms(&tracker, 12, 5050000ULL));
}

TEST_CASE("stratum_timing invalid timestamps and null safety", "[stratum_timing]")
{
    stratum_timing_tracker_t tracker;
    stratum_timing_reset(&tracker);

    // NULL tracker safety
    stratum_timing_record(NULL, 1, 1000ULL);
    stratum_timing_reset(NULL);
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, stratum_timing_calculate_ms(NULL, 1, 2000ULL));

    // Backward clock jump (now_us < submit_time_us)
    stratum_timing_record(&tracker, 7, 5000000ULL);
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, stratum_timing_calculate_ms(&tracker, 7, 4999999ULL));

    // Non-existent ID
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, stratum_timing_calculate_ms(&tracker, 999, 6000000ULL));
}
