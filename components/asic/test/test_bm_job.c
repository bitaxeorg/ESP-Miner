#include "unity.h"
#include "bitmain_job_test_bindings.h"
#include "bm_job.h"
#include "job_pipeline_test_harness.h"
#include <string.h>
#undef malloc
#undef strdup

TEST_CASE("common Bitmain conversion matches the existing constructor across rolling modes",
          "[asic-job][bitmain]")
{
    const uint8_t counts[] = {0, 1, 4, 5};
    for (int type = JOB_TYPE_V1; type <= JOB_TYPE_SV2_EXTENDED; ++type) {
        for (size_t n = 0; n < sizeof(counts); ++n) {
            for (unsigned rolling = 0; rolling < 2; ++rolling) {
                asic_job_t source = {
                    .source_type = (mining_job_source_t)type,
                    .version = 0x20000004, .version_mask = rolling ? 0x1fffe000 : 0,
                    .ntime = 0x64658bd8, .nbits = 0x1705dd01,
                    .pool_diff = 2048, .pool_id = 3,
                    .job_id = "42", .extranonce2 = "aabb",
                };
                memset(source.prev_hash, 0x5a, 32);
                memset(source.merkle_root, 0x71, 32);
                miner_job_t legacy = {
                    .type = (miner_job_type_t)type, .version = source.version,
                    .version_mask = source.version_mask, .ntime = source.ntime,
                    .nbits = source.nbits, .pool_diff = source.pool_diff, .pool_id = source.pool_id,
                };
                memcpy(legacy.prev_hash, source.prev_hash, 32);
                bm_job expected = {0}, actual = {0};
                construct_bm_job_from_miner_job(&legacy, source.version, source.merkle_root,
                    source.version_mask, source.pool_diff, counts[n], &expected);
                bitmain_job_allocator_fault_injector_reset(0);
                TEST_ASSERT_TRUE(bm_job_build_from_asic_job(&source, counts[n], &actual));
                TEST_ASSERT_EQUAL_HEX32(expected.version, actual.version);
                TEST_ASSERT_EQUAL_HEX32(expected.version_mask, actual.version_mask);
                TEST_ASSERT_EQUAL_HEX32(expected.ntime, actual.ntime);
                TEST_ASSERT_EQUAL_HEX32(expected.target, actual.target);
                TEST_ASSERT_EQUAL_HEX32(expected.starting_nonce, actual.starting_nonce);
                TEST_ASSERT_EQUAL_MEMORY(expected.prev_block_hash, actual.prev_block_hash, 32);
                TEST_ASSERT_EQUAL_MEMORY(expected.merkle_root, actual.merkle_root, 32);
                TEST_ASSERT_EQUAL_UINT8(expected.num_midstates, actual.num_midstates);
                TEST_ASSERT_EQUAL_MEMORY(expected.midstates, actual.midstates, sizeof(actual.midstates));
                TEST_ASSERT_EQUAL_DOUBLE(expected.pool_diff, actual.pool_diff);
                TEST_ASSERT_EQUAL_UINT8(expected.pool_id, actual.pool_id);
                TEST_ASSERT_EQUAL(expected.job_type, actual.job_type);
                memset(&source, 0, sizeof(source));
                TEST_ASSERT_EQUAL_STRING("42", actual.jobid);
                TEST_ASSERT_EQUAL_STRING("aabb", actual.extranonce2);
                free(actual.jobid);
                free(actual.extranonce2);
            }
        }
    }
}

TEST_CASE("Bitmain conversion rejects invalid metadata and releases partial allocations",
          "[asic-job][bitmain]")
{
    asic_job_t source = {0};
    bm_job output, original;
    memset(&output, 0xa5, sizeof(output));
    memcpy(&original, &output, sizeof(output));
    TEST_ASSERT_FALSE(bm_job_build_from_asic_job(NULL, 4, &output));
    TEST_ASSERT_FALSE(bm_job_build_from_asic_job(&source, 4, NULL));
    memset(source.job_id, 'x', sizeof(source.job_id));
    TEST_ASSERT_FALSE(bm_job_build_from_asic_job(&source, 4, &output));
    source.job_id[0] = 0;
    memset(source.extranonce2, 'a', sizeof(source.extranonce2));
    TEST_ASSERT_FALSE(bm_job_build_from_asic_job(&source, 4, &output));
    source.extranonce2[0] = 0;
    for (size_t failure = 1; failure <= 2; ++failure) {
        bitmain_job_allocator_fault_injector_reset(failure);
        TEST_ASSERT_FALSE(bm_job_build_from_asic_job(&source, 4, &output));
        TEST_ASSERT_EQUAL_UINT32(2, bitmain_job_allocator_fault_injector_calls());
        TEST_ASSERT_EQUAL_MEMORY(&original, &output, sizeof(output));
    }
    bitmain_job_allocator_fault_injector_reset(0);
    TEST_ASSERT_TRUE(bm_job_build_from_asic_job(&source, 4, &output));
    TEST_ASSERT_EQUAL_UINT8(1, output.num_midstates);
    free(output.jobid);
    free(output.extranonce2);
}

TEST_CASE("common send adapter transfers only complete independent jobs and recovers",
          "[asic-job][bitmain]")
{
    asic_job_t source = {
        .version = 0x20000004, .starting_nonce = 0x12345678,
        .job_id = "send", .extranonce2 = "aabbcc", .pool_diff = 256,
    };
    job_pipeline_harness_result_t result;
    for (size_t failure = 1; failure <= 3; ++failure) {
        bitmain_job_allocator_fault_injector_reset(failure);
        job_pipeline_harness_send_common(4, &source, &result);
        TEST_ASSERT_EQUAL_UINT32(0, result.job_count);
    }
    bitmain_job_allocator_fault_injector_reset(0);
    job_pipeline_harness_send_common(4, NULL, &result);
    TEST_ASSERT_EQUAL_UINT32(0, result.job_count);
    job_pipeline_harness_send_common(4, &source, &result);
    TEST_ASSERT_EQUAL_UINT32(1, result.job_count);
    memset(&source, 0, sizeof(source));
    TEST_ASSERT_EQUAL_STRING("send", result.jobs[0]->jobid);
    TEST_ASSERT_EQUAL_STRING("aabbcc", result.jobs[0]->extranonce2);
    TEST_ASSERT_EQUAL_UINT32(0x12345678, result.jobs[0]->starting_nonce);
    TEST_ASSERT_EQUAL_UINT32(0x20000004, result.jobs[0]->version);
    TEST_ASSERT_EQUAL_DOUBLE(256, result.jobs[0]->pool_diff);
    job_pipeline_harness_result_free(&result);
    bitmain_job_allocator_fault_injector_reset(0);
}
