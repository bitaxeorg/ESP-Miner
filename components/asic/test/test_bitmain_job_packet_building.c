#include "unity.h"
#include "bitmain_job_packet.h"
#include "utils.h"
#include <stddef.h>
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
    bm1397_job_packet_t job;
    bm1397_build_job_packet(&mjob, 4, 1, &job);

    uint8_t expected_midstate_bin[32];
    hex2bin("91DFEA528A9F73683D0D495DD6DD7415E1CA21CB411759E3E05D7D5FF285314D", expected_midstate_bin, 32);
    // bytes are reversed for the midstate on the bm job command packet
    uint8_t expected_midstate_bin_reversed[32];
    reverse_32bit_words(expected_midstate_bin, expected_midstate_bin_reversed);
    reverse_endianness_per_word(expected_midstate_bin_reversed);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_midstate_bin_reversed, job.midstates[0], 32);
}

TEST_CASE("BM1397 packet limits midstates and clears unused entries",
          "[asic-job][bitmain]")
{
    const uint8_t requested[] = {0, 1, 4, 5, UINT8_MAX};
    const uint8_t zeros[BM1397_NUM_MIDSTATES][32] = {0};
    asic_job_t source = {.version = 0x20000000};
    for (unsigned rolling = 0; rolling < 2; ++rolling) {
        source.version_mask = rolling ? 0x1fffe000 : 0;
        for (size_t n = 0; n < sizeof(requested); ++n) {
            bm1397_job_packet_t packet;
            memset(&packet, 0xa5, sizeof(packet));
            bm1397_build_job_packet(&source, 0x7c, requested[n], &packet);
            uint8_t expected_count = requested[n] == 0 ? 0 :
                (!rolling || requested[n] == 1 ? 1 : BM1397_NUM_MIDSTATES);
            TEST_ASSERT_EQUAL_HEX8(0x7c, packet.job_id);
            TEST_ASSERT_EQUAL_UINT8(expected_count, packet.num_midstates);
            for (uint8_t i = expected_count; i < BM1397_NUM_MIDSTATES; ++i) {
                TEST_ASSERT_EQUAL_HEX8_ARRAY(zeros[i], packet.midstates[i], 32);
            }
        }
    }
}

TEST_CASE("Bitmain packet builders support unaligned outputs without overwriting guards",
          "[asic-job][bitmain]")
{
    asic_job_t source = {
        .version = 0x3000a004, .version_mask = 0x1000a000,
        .nbits = 0x1705dd01, .ntime = 0x64658bd8,
        .starting_nonce = 0x12345678,
    };
    for (unsigned i = 0; i < 32; ++i) {
        source.prev_hash[i] = i;
        source.merkle_root[i] = 63 - i;
    }
    bm13xx_job_packet_t expected13;
    bm1397_job_packet_t expected97;
    bm13xx_build_job_packet(&source, 8, &expected13);
    bm1397_build_job_packet(&source, 4, 4, &expected97);
    _Alignas(uint32_t) uint8_t storage[sizeof(expected97) + 8];
    for (size_t offset = 1; offset <= 4; ++offset) {
        memset(storage, 0xa5, sizeof(storage));
        bm13xx_build_job_packet(&source, 8, (bm13xx_job_packet_t *)(storage + offset));
        TEST_ASSERT_EQUAL_HEX8_ARRAY(&expected13, storage + offset, sizeof(expected13));
        for (size_t i = 0; i < sizeof(storage); ++i) {
            if (i < offset || i >= offset + sizeof(expected13)) {
                TEST_ASSERT_EQUAL_HEX8(0xa5, storage[i]);
            }
        }
        memset(storage, 0xa5, sizeof(storage));
        bm1397_build_job_packet(&source, 4, 4, (bm1397_job_packet_t *)(storage + offset));
        TEST_ASSERT_EQUAL_HEX8_ARRAY(&expected97, storage + offset, sizeof(expected97));
        for (size_t i = 0; i < sizeof(storage); ++i) {
            if (i < offset || i >= offset + sizeof(expected97)) {
                TEST_ASSERT_EQUAL_HEX8(0xa5, storage[i]);
            }
        }
    }
}
