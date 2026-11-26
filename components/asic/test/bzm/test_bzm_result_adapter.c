#include <string.h>
#include "unity.h"
#include "bzm/result.h"

TEST_CASE("BZM result resolves rolling into an owned PR1972 snapshot", "[asic][bzm][result]")
{
    bzm_result_t share = {
        .job_valid = true, .job = {.version = 0x20000000, .ntime = 1234,
            .version_mask = 0x1fffe000, .source_type = JOB_TYPE_SV2_STANDARD,
            .job_id = "42", .extranonce2 = "001122", .pool_id = 2},
        .final_ntime = 1238, .final_version = 0x20006000, .nonce = 0x12345678,
        .asic_index = 3, .engine_id = 235, .micro_job_id = 3, .timestamp_us = 99,
    };
    task_result result;
    TEST_ASSERT_TRUE(bzm_result_to_task(&share, &result));
    TEST_ASSERT_EQUAL_UINT32(1234, share.job.ntime);
    TEST_ASSERT_EQUAL_UINT32(1238, result.job.ntime);
    TEST_ASSERT_EQUAL_HEX32(0x20006000, result.rolled_version);
    TEST_ASSERT_EQUAL_UINT32(0x12345678, result.nonce);
    TEST_ASSERT_EQUAL_UINT8(3, result.asic_nr);
    TEST_ASSERT_EQUAL_UINT8(235, result.core_id);
    TEST_ASSERT_EQUAL_UINT8(3, result.small_core_id);
    TEST_ASSERT_TRUE(result.timestamp_us == UINT64_C(99));
    TEST_ASSERT_EQUAL(REGISTER_INVALID, result.register_type);
    TEST_ASSERT_EQUAL(JOB_TYPE_SV2_STANDARD, result.job.source_type);
    TEST_ASSERT_EQUAL_UINT8(2, result.job.pool_id);
    memset(&share, 0, sizeof(share));
    TEST_ASSERT_EQUAL_STRING("42", result.job.job_id);
    TEST_ASSERT_EQUAL_STRING("001122", result.job.extranonce2);
    uint8_t header[80];
    asic_job_header(&result.job, result.nonce, result.rolled_version, header);
    TEST_ASSERT_EQUAL_HEX8(0xd6, header[68]); /* 1238, little endian */
    TEST_ASSERT_EQUAL_HEX8(0x60, header[1]);
}

TEST_CASE("BZM result rejects absent snapshots without changing output", "[asic][bzm][result]")
{
    bzm_result_t share = {0};
    task_result result = {.nonce = 123};
    TEST_ASSERT_FALSE(bzm_result_to_task(NULL, &result));
    TEST_ASSERT_FALSE(bzm_result_to_task(&share, NULL));
    TEST_ASSERT_FALSE(bzm_result_to_task(&share, &result));
    TEST_ASSERT_EQUAL_UINT32(123, result.nonce);
}
