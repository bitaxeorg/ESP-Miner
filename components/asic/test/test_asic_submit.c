#include "unity.h"
#include "bitmain_job_test_bindings.h"
#include "job_pipeline_test_harness.h"
#include <string.h>
#undef malloc

TEST_CASE("job submission rejects unterminated metadata before allocation",
          "[asic-job][ownership]")
{
    asic_job_t source = {0};
    job_pipeline_harness_result_t result;
    bitmain_job_allocator_fault_injector_reset(0);
    memset(source.job_id, 'x', sizeof(source.job_id));
    job_pipeline_harness_send_job(&source, &result);
    TEST_ASSERT_EQUAL_UINT32(0, result.job_count);
    source.job_id[0] = 0;
    memset(source.extranonce2, 'a', sizeof(source.extranonce2));
    job_pipeline_harness_send_job(&source, &result);
    TEST_ASSERT_EQUAL_UINT32(0, result.job_count);
    TEST_ASSERT_EQUAL_UINT32(0, bitmain_job_allocator_fault_injector_calls());
    source.extranonce2[0] = 0;
    job_pipeline_harness_send_job(&source, &result);
    TEST_ASSERT_EQUAL_UINT32(1, result.job_count);
    TEST_ASSERT_EQUAL_STRING("", result.jobs[0]->job_id);
    TEST_ASSERT_EQUAL_STRING("", result.jobs[0]->extranonce2);
    TEST_ASSERT_EQUAL_HEX32(0, result.jobs[0]->version);
    TEST_ASSERT_EQUAL_HEX32(0, result.jobs[0]->version_mask);
    TEST_ASSERT_EQUAL_DOUBLE(0, result.jobs[0]->pool_diff);
    job_pipeline_harness_result_free(&result);
    bitmain_job_allocator_fault_injector_reset(0);
}

TEST_CASE("job submission owns metadata and recovers from allocation failures",
          "[asic-job][bitmain]")
{
    asic_job_t source = {
        .version = 0x20000004, .starting_nonce = 0x12345678,
        .job_id = "send", .extranonce2 = "aabbcc", .pool_diff = 256,
    };
    job_pipeline_harness_result_t result;
    bitmain_job_allocator_fault_injector_reset(1);
    job_pipeline_harness_send_job(&source, &result);
    TEST_ASSERT_EQUAL_UINT32(0, result.job_count);
    TEST_ASSERT_EQUAL_UINT32(1, bitmain_job_allocator_fault_injector_calls());
    bitmain_job_allocator_fault_injector_reset(0);
    job_pipeline_harness_send_job(NULL, &result);
    TEST_ASSERT_EQUAL_UINT32(0, result.job_count);
    bitmain_job_allocator_fault_injector_reset(2);
    job_pipeline_harness_send_job(&source, &result);
    TEST_ASSERT_EQUAL_UINT32(1, result.job_count);
    TEST_ASSERT_EQUAL_UINT32(1, bitmain_job_allocator_fault_injector_calls());
    memset(&source, 0, sizeof(source));
    TEST_ASSERT_EQUAL_STRING("send", result.jobs[0]->job_id);
    TEST_ASSERT_EQUAL_STRING("aabbcc", result.jobs[0]->extranonce2);
    TEST_ASSERT_EQUAL_UINT32(0x12345678, result.jobs[0]->starting_nonce);
    TEST_ASSERT_EQUAL_UINT32(0x20000004, result.jobs[0]->version);
    TEST_ASSERT_EQUAL_DOUBLE(256, result.jobs[0]->pool_diff);
    job_pipeline_harness_result_free(&result);
    bitmain_job_allocator_fault_injector_reset(0);
}
