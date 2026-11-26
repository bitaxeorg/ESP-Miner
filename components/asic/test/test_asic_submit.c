#include "unity.h"
#include "job_pipeline_test_harness.h"
#include "utils.h"
#include <string.h>

TEST_CASE("job submission rejects unterminated metadata before sending",
          "[asic-job][ownership]")
{
    asic_job_t source = {0};
    job_pipeline_harness_result_t result;
    memset(source.job_id, 'x', sizeof(source.job_id));
    job_pipeline_harness_send_job(&source, &result);
    TEST_ASSERT_EQUAL_UINT32(0, result.job_count);
    source.job_id[0] = 0;
    memset(source.extranonce2, 'a', sizeof(source.extranonce2));
    job_pipeline_harness_send_job(&source, &result);
    TEST_ASSERT_EQUAL_UINT32(0, result.job_count);
    source.extranonce2[0] = 0;
    job_pipeline_harness_send_job(&source, &result);
    TEST_ASSERT_EQUAL_UINT32(1, result.job_count);
    TEST_ASSERT_EQUAL_STRING("", result.jobs[0]->job_id);
    TEST_ASSERT_EQUAL_STRING("", result.jobs[0]->extranonce2);
    TEST_ASSERT_EQUAL_HEX32(0, result.jobs[0]->version);
    TEST_ASSERT_EQUAL_HEX32(0, result.jobs[0]->version_mask);
    uint8_t zero_target[32] = {0};
    TEST_ASSERT_EQUAL_MEMORY(zero_target, result.jobs[0]->pool_target, 32);
    job_pipeline_harness_result_free(&result);
}

TEST_CASE("job submission forwards valid metadata",
          "[asic-job][bitmain]")
{
    asic_job_t source = {
        .version = 0x20000004, .starting_nonce = 0x12345678,
        .job_id = "send", .extranonce2 = "aabbcc",
    };
    diff_to_target(256.0, source.pool_target);
    job_pipeline_harness_result_t result;
    job_pipeline_harness_send_job(NULL, &result);
    TEST_ASSERT_EQUAL_UINT32(0, result.job_count);
    job_pipeline_harness_send_job(&source, &result);
    TEST_ASSERT_EQUAL_UINT32(1, result.job_count);
    memset(&source, 0, sizeof(source));
    TEST_ASSERT_EQUAL_STRING("send", result.jobs[0]->job_id);
    TEST_ASSERT_EQUAL_STRING("aabbcc", result.jobs[0]->extranonce2);
    TEST_ASSERT_EQUAL_UINT32(0x12345678, result.jobs[0]->starting_nonce);
    TEST_ASSERT_EQUAL_UINT32(0x20000004, result.jobs[0]->version);
    uint8_t expected_target[32];
    diff_to_target(256.0, expected_target);
    TEST_ASSERT_EQUAL_MEMORY(expected_target, result.jobs[0]->pool_target, 32);
    job_pipeline_harness_result_free(&result);
}
