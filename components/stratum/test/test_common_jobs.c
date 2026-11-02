#include "unity.h"
#include "mining_test_bindings.h"
#include "mining.h"
#include "utils.h"
#include <string.h>

TEST_CASE("job rolling requires a job extranonce and coinbase prefix together",
          "[mining][asic-job]")
{
    miner_job_t source = {0};
    TEST_ASSERT_FALSE(miner_job_is_rollable(NULL));
    TEST_ASSERT_FALSE(miner_job_is_rollable(&source));
    source.extranonce2_len = 1;
    TEST_ASSERT_FALSE(miner_job_is_rollable(&source));
    source.coinbase_prefix_len = 1;
    TEST_ASSERT_TRUE(miner_job_is_rollable(&source));
    source.extranonce2_len = 0;
    TEST_ASSERT_FALSE(miner_job_is_rollable(&source));
}

TEST_CASE("job builder rejects invalid metadata without changing output",
          "[mining][asic-job]")
{
    miner_job_t source = { .type = JOB_TYPE_SV2_STANDARD };
    asic_job_t output, original;
    memset(&output, 0xa5, sizeof(output));
    memcpy(&original, &output, sizeof(output));
    TEST_ASSERT_FALSE(mining_build_asic_job(NULL, 0, 0, &output));
    TEST_ASSERT_FALSE(mining_build_asic_job(&source, 0, 0, NULL));
    memset(source.job_id, 'x', sizeof(source.job_id));
    TEST_ASSERT_FALSE(mining_build_asic_job(&source, 0, 0, &output));
    source.job_id[0] = 0;
    source.type = JOB_TYPE_V1;
    source.extranonce2_len = 33;
    TEST_ASSERT_FALSE(mining_build_asic_job(&source, 0, 0, &output));
    TEST_ASSERT_EQUAL_MEMORY(&original, &output, sizeof(output));
}

TEST_CASE("standard jobs copy header fields and submission metadata",
          "[mining][asic-job]")
{
    miner_job_t source = {
        .type = JOB_TYPE_SV2_STANDARD, .job_id = "standard",
        .version = 0x20000004, .version_mask = 0x1fffe000,
        .ntime = 0x64658bd8, .nbits = 0x1705dd01,
        .pool_id = 255, .extranonce2_len = 255,
    };
    diff_to_target(16.0, source.pool_target);
    memset(source.prev_hash, 0x5a, sizeof(source.prev_hash));
    memset(source.merkle_root, 0x71, sizeof(source.merkle_root));
    asic_job_t job;
    TEST_ASSERT_TRUE(mining_build_asic_job(&source, UINT64_MAX, 0, &job));
    TEST_ASSERT_EQUAL_HEX32(source.version, job.version);
    TEST_ASSERT_EQUAL_HEX32(source.version_mask, job.version_mask);
    TEST_ASSERT_EQUAL_HEX32(source.ntime, job.ntime);
    TEST_ASSERT_EQUAL_HEX32(source.nbits, job.nbits);
    TEST_ASSERT_EQUAL_UINT32(0, job.starting_nonce);
    TEST_ASSERT_EQUAL_MEMORY(source.pool_target, job.pool_target, 32);
    TEST_ASSERT_EQUAL_UINT8(255, job.pool_id);
    TEST_ASSERT_EQUAL(JOB_TYPE_SV2_STANDARD, job.source_type);
    TEST_ASSERT_EQUAL_STRING("", job.extranonce2);
    TEST_ASSERT_EQUAL_MEMORY(source.prev_hash, job.prev_hash, 32);
    TEST_ASSERT_EQUAL_MEMORY(source.merkle_root, job.merkle_root, 32);
    memset(source.prev_hash, 0, 32);
    memset(source.job_id, 0, sizeof(source.job_id));
    TEST_ASSERT_EQUAL_UINT8(0x5a, job.prev_hash[0]);
    TEST_ASSERT_EQUAL_STRING("standard", job.job_id);
    TEST_ASSERT_TRUE(mining_build_asic_job(&source, 0, 0x20002004, &job));
    TEST_ASSERT_EQUAL_HEX32(0x20002004, job.version);
}

TEST_CASE("job builder encodes zero short and wide extranonces",
          "[mining][asic-job]")
{
    const uint8_t lengths[] = {0, 1, 8, 9, 32};
    const char *expected[] = {
        "", "08", "0807060504030201", "080706050403020100",
        "0807060504030201000000000000000000000000000000000000000000000000",
    };
    uint8_t prefix = 1, suffix = 2;
    miner_job_t source = {
        .type = JOB_TYPE_SV2_EXTENDED, .job_id = "extended",
        .coinbase_prefix = &prefix, .coinbase_prefix_len = 1,
        .coinbase_suffix = &suffix, .coinbase_suffix_len = 1,
    };
    mining_allocator_fault_injector_reset(0);
    for (size_t i = 0; i < sizeof(lengths); ++i) {
        source.extranonce2_len = lengths[i];
        asic_job_t job;
        TEST_ASSERT_TRUE(mining_build_asic_job(&source, 0x0102030405060708ULL, 0, &job));
        TEST_ASSERT_EQUAL_STRING(expected[i], job.extranonce2);
        TEST_ASSERT_EQUAL(JOB_TYPE_SV2_EXTENDED, job.source_type);
    }
    TEST_ASSERT_EQUAL_UINT32(0, mining_allocator_fault_injector_calls());
}

TEST_CASE("coinbase hash failures preserve common job output and permit recovery",
          "[mining][asic-job]")
{
    uint8_t prefix[] = {1, 2}, suffix[] = {6, 7}, expected_hash[32];
    TEST_ASSERT_EQUAL_UINT32(32, hex2bin(
        "a669a4141f56b33d635a76c538dabafb72d136de59cc6373260376376dcba307",
        expected_hash, sizeof(expected_hash)));
    miner_job_t source = {
        .job_id = "hash-failure",
        .coinbase_prefix = prefix, .coinbase_prefix_len = sizeof(prefix),
        .extranonce1 = {3}, .extranonce1_len = 1, .extranonce2_len = 2,
        .coinbase_suffix = suffix, .coinbase_suffix_len = sizeof(suffix),
    };
    const miner_job_type_t types[] = {JOB_TYPE_V1, JOB_TYPE_SV2_EXTENDED};
    asic_job_t job, original;
    memset(&original, 0xa5, sizeof(original));
    for (size_t type = 0; type < sizeof(types) / sizeof(types[0]); ++type) {
        source.type = types[type];
        for (size_t failure_at = 1; failure_at <= 7; ++failure_at) {
            memcpy(&job, &original, sizeof(job));
            mining_hash_fault_injector_reset(failure_at);
            TEST_ASSERT_FALSE(mining_build_asic_job(&source, 0x0504, 0, &job));
            TEST_ASSERT_EQUAL_MEMORY(&original, &job, sizeof(job));
            TEST_ASSERT_EQUAL_UINT32(failure_at <= 6 ? 1 : 0, mining_hash_fault_injector_abort_calls());
            TEST_ASSERT_TRUE(mining_build_asic_job(&source, 0x0504, 0, &job));
            TEST_ASSERT_EQUAL_MEMORY(expected_hash, job.merkle_root, sizeof(expected_hash));
            TEST_ASSERT_EQUAL_STRING("0405", job.extranonce2);
        }
    }
    mining_hash_fault_injector_reset(0);
}
