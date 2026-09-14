#include "unity.h"
#include "bm_job.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>

// Values calculated from esp-miner/components/asic/test/verifiers/bm1397.py
TEST_CASE("Validate midstate generation", "[asic-job][bitmain]")
{
    asic_job_t mjob;
    memset(&mjob, 0, sizeof(mjob));
    hex2bin("bf44fd3513dc7b837d60e5c628b572b448d204a8000007490000000000000000", mjob.prev_hash, 32);
    reverse_endianness_per_word(mjob.prev_hash);
    mjob.version = 0x20000004;
    mjob.nbits = 0x1705dd01;
    mjob.ntime = 0x64658bd8;
    mjob.pool_diff = 1000;

    hex2bin("cd1be82132ef0d12053dcece1fa0247fcfdb61d4dbd3eb32ea9ef9b4c604a846", mjob.merkle_root, 32);
    bm_job job = { 0 };
    TEST_ASSERT_TRUE(bm_job_build_from_asic_job(&mjob, 1, &job));

    uint8_t expected_midstate_bin[32];
    hex2bin("91DFEA528A9F73683D0D495DD6DD7415E1CA21CB411759E3E05D7D5FF285314D", expected_midstate_bin, 32);
    // bytes are reversed for the midstate on the bm job command packet
    uint8_t expected_midstate_bin_reversed[32];
    reverse_32bit_words(expected_midstate_bin, expected_midstate_bin_reversed);
    reverse_endianness_per_word(expected_midstate_bin_reversed);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_midstate_bin_reversed, job.midstates[0], 32);
}

TEST_CASE("Bitmain conversion copies zero and nonzero header fields",
          "[asic-job][bitmain]")
{
    asic_job_t source = {
        .version = 0, .version_mask = 0, .pool_diff = 0,
        .nbits = 0x1705dd01, .ntime = 0x64658bd8, .pool_id = 3,
        .source_type = JOB_TYPE_SV2_STANDARD,
    };
    bm_job result;
    TEST_ASSERT_TRUE(bm_job_build_from_asic_job(&source, 0, &result));
    TEST_ASSERT_EQUAL_HEX32(0, result.version);
    TEST_ASSERT_EQUAL_HEX32(0, result.version_mask);
    TEST_ASSERT_EQUAL_DOUBLE(0, result.pool_diff);
    TEST_ASSERT_EQUAL_UINT8(0, result.num_midstates);
    TEST_ASSERT_EQUAL_HEX32(source.nbits, result.nbits);
    TEST_ASSERT_EQUAL_HEX32(source.ntime, result.ntime);
    TEST_ASSERT_EQUAL_UINT8(source.pool_id, result.pool_id);
    TEST_ASSERT_EQUAL_INT(source.source_type, result.job_type);
    TEST_ASSERT_EQUAL_UINT32(0, result.starting_nonce);

    source.version = 0x20002000;
    source.version_mask = 0x1fffe000;
    source.pool_diff = 512.0;
    TEST_ASSERT_TRUE(bm_job_build_from_asic_job(&source, 0, &result));
    TEST_ASSERT_EQUAL_HEX32(source.version, result.version);
    TEST_ASSERT_EQUAL_HEX32(source.version_mask, result.version_mask);
    TEST_ASSERT_EQUAL_DOUBLE(source.pool_diff, result.pool_diff);
}

TEST_CASE("Bitmain software midstate count honors zero mask and the buffer limit",
          "[asic-job][bitmain]")
{
    asic_job_t source = { .version = 0x20000000 };
    bm_job first, limited;
    TEST_ASSERT_TRUE(bm_job_build_from_asic_job(&source, 4, &first));
    TEST_ASSERT_EQUAL_UINT8(1, first.num_midstates);

    source.version_mask = 0x1fffe000;
    TEST_ASSERT_TRUE(bm_job_build_from_asic_job(&source, BM_JOB_MAX_MIDSTATES, &first));
    TEST_ASSERT_TRUE(bm_job_build_from_asic_job(&source, UINT8_MAX, &limited));
    TEST_ASSERT_EQUAL_UINT8(BM_JOB_MAX_MIDSTATES, first.num_midstates);
    TEST_ASSERT_EQUAL_UINT8(BM_JOB_MAX_MIDSTATES, limited.num_midstates);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(first.midstates, limited.midstates, sizeof(first.midstates));
}
