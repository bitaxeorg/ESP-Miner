#include <string.h>

#include "bzm/result_dedup.h"
#include "unity.h"

static bzm_result_dedup_t cache;

static asic_job_t job_fixture(void)
{
    asic_job_t job = {.version = 0x20000000, .ntime = 100,
        .version_mask = 0x1fffe000,
        .pool_id = 1, .source_type = JOB_TYPE_V1};
    strcpy(job.job_id, "1001");
    return job;
}

static bzm_result_t result_fixture(void)
{
    return (bzm_result_t){.work_handle = 1, .nonce = 42,
        .final_ntime = 101, .final_version = 0x20002000};
}

TEST_CASE("BZM rejects the same header from different engine assignments", "[asic][bzm][dedup]")
{
    memset(&cache, 0, sizeof(cache));
    asic_job_t job = job_fixture();
    bzm_result_t result = result_fixture();
    TEST_ASSERT_FALSE(bzm_result_is_duplicate(&cache, &job, &result));
    result.work_handle = 900;
    result.engine_id = 47;
    result.asic_index = 3;
    result.sequence_id = 2;
    result.micro_job_id = 3;
    result.timestamp_us = 123456;
    // Different base offsets still describe the same actual mined header.
    job.version = result.final_version;
    job.ntime = result.final_ntime;
    TEST_ASSERT_TRUE(bzm_result_is_duplicate(&cache, &job, &result));
}

TEST_CASE("BZM duplicate identity preserves distinct pool jobs and headers", "[asic][bzm][dedup]")
{
    for (unsigned field = 0; field < 10; ++field) {
        memset(&cache, 0, sizeof(cache));
        asic_job_t job = job_fixture();
        bzm_result_t result = result_fixture();
        TEST_ASSERT_FALSE(bzm_result_is_duplicate(&cache, &job, &result));
        switch (field) {
            case 0: ++job.source_type; break;
            case 1: ++job.pool_id; break;
            case 2: strcpy(job.job_id, "1002"); break;
            case 3: strcpy(job.extranonce2, "01"); break;
            case 4: ++result.nonce; break;
            case 5: ++result.final_ntime; break;
            case 6: result.final_version ^= 0x4000; break;
            case 7: ++job.merkle_root[0]; break;
            case 8: ++job.prev_hash[0]; break;
            case 9: ++job.nbits; break;
        }
        TEST_ASSERT_FALSE(bzm_result_is_duplicate(&cache, &job, &result));
        TEST_ASSERT_TRUE(bzm_result_is_duplicate(&cache, &job, &result));
    }
}

TEST_CASE("BZM duplicate cache has bounded eviction and explicit logical reset", "[asic][bzm][dedup]")
{
    memset(&cache, 0, sizeof(cache));
    asic_job_t job = job_fixture();
    bzm_result_t result = result_fixture();
    for (uint32_t nonce = 0; nonce < BZM_RESULT_DEDUP_CAPACITY; ++nonce) {
        result.nonce = nonce;
        TEST_ASSERT_FALSE(bzm_result_is_duplicate(&cache, &job, &result));
    }
    result.nonce = 0;
    TEST_ASSERT_TRUE(bzm_result_is_duplicate(&cache, &job, &result));
    result.nonce = BZM_RESULT_DEDUP_CAPACITY;
    TEST_ASSERT_FALSE(bzm_result_is_duplicate(&cache, &job, &result));
    result.nonce = 0;
    TEST_ASSERT_FALSE(bzm_result_is_duplicate(&cache, &job, &result));
    TEST_ASSERT_TRUE(bzm_result_is_duplicate(&cache, &job, &result));
    memset(&cache, 0, sizeof(cache));
    TEST_ASSERT_FALSE(bzm_result_is_duplicate(&cache, &job, &result));
}

TEST_CASE("BZM duplicate cache rejects invalid metadata without consuming entries", "[asic][bzm][dedup]")
{
    memset(&cache, 0, sizeof(cache));
    asic_job_t job = job_fixture();
    bzm_result_t result = result_fixture();
    TEST_ASSERT_FALSE(bzm_result_is_duplicate(NULL, &job, &result));
    TEST_ASSERT_FALSE(bzm_result_is_duplicate(&cache, NULL, &result));
    TEST_ASSERT_FALSE(bzm_result_is_duplicate(&cache, &job, NULL));
    memset(job.job_id, 'a', sizeof(job.job_id));
    TEST_ASSERT_FALSE(bzm_result_is_duplicate(&cache, &job, &result));
    job = job_fixture();
    memset(job.extranonce2, 'a', sizeof(job.extranonce2));
    TEST_ASSERT_FALSE(bzm_result_is_duplicate(&cache, &job, &result));
    TEST_ASSERT_EQUAL_UINT32(0, cache.next);
}
