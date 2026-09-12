#include "unity.h"
#include "asic_job.h"

TEST_CASE("common header uses exact hash bytes and little endian integers",
          "[asic-job]")
{
    asic_job_t job = {
        .version = 0xffffffff, .ntime = 0x48474645, .nbits = 0x4c4b4a49,
    };
    uint8_t expected[80], actual[80];
    for (unsigned i = 0; i < 80; ++i) expected[i] = (uint8_t)(i + 1);
    for (unsigned i = 0; i < 32; ++i) {
        job.prev_hash[i] = (uint8_t)(i + 5);
        job.merkle_root[i] = (uint8_t)(i + 37);
    }
    asic_job_header(&job, 0x504f4e4d, 0x04030201, actual);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, actual, 80);
}
