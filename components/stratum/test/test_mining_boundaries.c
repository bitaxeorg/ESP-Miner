#include "unity.h"
#include "mining.h"
#include "miner_job.h"
#include "job_pipeline_test_harness.h"

#include <stdio.h>
#include <string.h>

// Independent reference: walk only the permitted bits, propagating carry.
static uint32_t increment_mask_reference(uint32_t value, uint32_t mask)
{
    for (uint32_t bit = 1; bit != 0; bit <<= 1) {
        if (mask & bit) {
            value ^= bit;
            if (value & bit) break;
        }
    }
    return value;
}

TEST_CASE("version increment preserves unmasked bits at gaps and wrap", "[mining]")
{
    TEST_ASSERT_EQUAL_HEX32(0x20000004, increment_bitmask(0x3fffe004, 0x1fffe000));
    TEST_ASSERT_EQUAL_HEX32(0x20010004, increment_bitmask(0x20002004, 0x00012000));
    TEST_ASSERT_EQUAL_HEX32(0x20000004, increment_bitmask(0x20000004, 0));
    TEST_ASSERT_EQUAL_HEX32(0, increment_bitmask(UINT32_MAX, UINT32_MAX));
    TEST_ASSERT_EQUAL_HEX32(0x20000004, increment_bitmask(0xa0000004, 0x80000000));

    const unsigned shifts[] = {0, 13, 24};
    for (size_t s = 0; s < sizeof(shifts) / sizeof(shifts[0]); ++s) {
        for (uint32_t bits = 0; bits <= 255; ++bits) {
            uint32_t mask = bits << shifts[s];
            for (uint32_t counter = 0; counter <= 255; ++counter) {
                uint32_t value = (0xa5a5a5a5u & ~mask) | ((counter << shifts[s]) & mask);
                uint32_t actual = increment_bitmask(value, mask);
                TEST_ASSERT_EQUAL_HEX32(increment_mask_reference(value, mask), actual);
                TEST_ASSERT_EQUAL_HEX32(value & ~mask, actual & ~mask);
            }
        }
    }
}

TEST_CASE("extranonce counter stops before serialized bytes wrap", "[mining]")
{
    static miner_job_t job;
    memset(&job, 0, sizeof(job));
    job.coinbase_prefix_len = 1;
    const uint8_t lengths[] = {1, 2, 4, 7, 8, 32};
    for (size_t i = 0; i < sizeof(lengths); ++i) {
        job.extranonce2_len = lengths[i];
        uint64_t last = lengths[i] < 8 ? (UINT64_C(1) << (8 * lengths[i])) - 1 : UINT64_MAX;
        uint64_t counter = last - 1;
        TEST_ASSERT_TRUE(miner_job_advance_extranonce2(&job, &counter));
        TEST_ASSERT_TRUE(counter == last);
        TEST_ASSERT_FALSE(miner_job_advance_extranonce2(&job, &counter));
        TEST_ASSERT_TRUE(counter == last);
    }
    uint64_t counter = 0;
    job.extranonce2_len = 0;
    TEST_ASSERT_FALSE(miner_job_advance_extranonce2(&job, &counter));
    TEST_ASSERT_FALSE(miner_job_advance_extranonce2(NULL, &counter));
    TEST_ASSERT_FALSE(miner_job_advance_extranonce2(&job, NULL));
    job.extranonce2_len = 1;
    counter = 256;
    TEST_ASSERT_FALSE(miner_job_advance_extranonce2(&job, &counter));
    TEST_ASSERT_TRUE(counter == 256);
}

TEST_CASE("job task waits at extranonce exhaustion and resumes on new work", "[mining][job-task]")
{
    miner_job_pool_init();
    for (size_t slot = 0; slot < 2; ++slot) {
        miner_job_t *job = miner_job_get_slot(slot);
        snprintf(job->job_id, sizeof(job->job_id), "boundary-%u", (unsigned)slot);
        job->type = JOB_TYPE_V1;
        job->version = 0x20000004;
        job->version_mask = 0x1fffe000;
        job->ntime = 0x64658bd8;
        job->nbits = 0x1705dd01;
        job->pool_diff = 256;
        job->clean_jobs = true;
        job->extranonce2_len = 1;
        job->coinbase_prefix_len = 1;
        job->coinbase_prefix[0] = 1;
        job->coinbase_suffix_len = 1;
        job->coinbase_suffix[0] = 2;
    }
    static job_pipeline_harness_event_t events[260];
    memset(events, 0, sizeof(events)); // TIMEOUT is zero.
    events[0] = (job_pipeline_harness_event_t){JOB_PIPELINE_HARNESS_NOTIFY, 0};
    events[259] = (job_pipeline_harness_event_t){JOB_PIPELINE_HARNESS_NOTIFY, 1};

    for (unsigned hardware = 0; hardware < 2; ++hardware) {
        job_pipeline_harness_result_t result;
        job_pipeline_harness_run((job_pipeline_harness_config_t){
            .hardware_version_rolling = hardware != 0,
            .software_midstates = hardware ? 0 : 4,
            .asic_initialized = true,
            .job_frequency_ms = 1,
            .count_only = true,
        }, events, sizeof(events) / sizeof(events[0]), &result);
        // 256 unique extranonces, three exhausted cycles, then one new job.
        TEST_ASSERT_EQUAL_UINT32(257, result.submitted_job_count);
        TEST_ASSERT_EQUAL_UINT32(2, result.coinbase_decode_count);
        TEST_ASSERT_EQUAL_UINT8(1, result.active_job_slot);
        job_pipeline_harness_result_free(&result);
    }
}
