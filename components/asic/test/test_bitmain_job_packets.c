#include "bitmain_job_packet.h"
#include "utils.h"
#include "global_state.h"
#include "unity.h"

#include "bm13xx_test_harness.h"
#include "bm1397_test_harness.h"
#include "mining.h"

#include <stdlib.h>
#include <string.h>

static asic_job_t *make_job(void)
{
    asic_job_t *job = calloc(1, sizeof(*job));
    TEST_ASSERT_NOT_NULL(job);
    job->version = 0x20000004;
    job->version_mask = 0x1fffe000;
    job->nbits = 0x1705dd01;
    job->ntime = 0x64658bd8;
    job->starting_nonce = 0x12345678;
    job->pool_diff = 256.125;
    job->pool_id = UINT8_MAX;
    job->source_type = JOB_TYPE_V1;
    strcpy(job->job_id, "packet-job");
    strcpy(job->extranonce2, "0001020304050607");
    static const uint8_t merkle_root[32] = {
        0x1c, 0x1d, 0x1e, 0x1f, 0x18, 0x19, 0x1a, 0x1b,
        0x14, 0x15, 0x16, 0x17, 0x10, 0x11, 0x12, 0x13,
        0x0c, 0x0d, 0x0e, 0x0f, 0x08, 0x09, 0x0a, 0x0b,
        0x04, 0x05, 0x06, 0x07, 0x00, 0x01, 0x02, 0x03,
    };
    static const uint8_t prev_hash[32] = {
        0x3c, 0x3d, 0x3e, 0x3f, 0x38, 0x39, 0x3a, 0x3b,
        0x34, 0x35, 0x36, 0x37, 0x30, 0x31, 0x32, 0x33,
        0x2c, 0x2d, 0x2e, 0x2f, 0x28, 0x29, 0x2a, 0x2b,
        0x24, 0x25, 0x26, 0x27, 0x20, 0x21, 0x22, 0x23,
    };
    memcpy(job->merkle_root, merkle_root, sizeof(merkle_root));
    memcpy(job->prev_hash, prev_hash, sizeof(prev_hash));
    return job;
}

static void queue_bm1397_job_response(uint8_t job_id, uint8_t midstate_index,
                                      uint32_t nonce)
{
    uint8_t response[BM1397_HARNESS_RESPONSE_SIZE] = {
        0xaa, 0x55, 0, 0, 0, 0, 0,
        (uint8_t)(job_id | midstate_index), 0x80,
    };
    memcpy(response + 2, &nonce, sizeof(nonce));
    bm1397_harness_queue_response(response);
}

TEST_CASE("BM13xx work packets preserve complete header fields byte exact",
          "[asic][job-packet][characterization]")
{
    static const uint8_t expected_template[] = {
        0x55, 0xaa, 0x21, 0x56, 0x00, 0x01,
        0x78, 0x56, 0x34, 0x12, 0x01, 0xdd, 0x05, 0x17,
        0xd8, 0x8b, 0x65, 0x64,
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
        0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
        0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
        0x04, 0x00, 0x00, 0x20, 0x00, 0x00,
    };
    static const struct {
        uint8_t job_id;
        uint8_t crc_high;
        uint8_t crc_low;
    } expected[] = {
        {0x08, 0xd6, 0x21},
        {0x18, 0x05, 0xd4},
        {0x18, 0x05, 0xd4},
        {0x18, 0x05, 0xd4},
    };

    for (size_t index = 0; index < BM13XX_HARNESS_DRIVER_COUNT; ++index) {
        const bm13xx_harness_driver_t *driver = &bm13xx_harness_drivers[index];
        GlobalState *state = bm13xx_harness_begin();

        asic_job_t *job = make_job();
        driver->send_work(state, job);

        TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, bm13xx_harness_packet_count(),
                                         driver->name);
        const bm13xx_harness_packet_t *packet = bm13xx_harness_packet(0);
        uint8_t expected_packet[sizeof(expected_template)];
        memcpy(expected_packet, expected_template, sizeof(expected_packet));
        expected_packet[4] = expected[index].job_id;
        expected_packet[sizeof(expected_packet) - 2] = expected[index].crc_high;
        expected_packet[sizeof(expected_packet) - 1] = expected[index].crc_low;
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(sizeof(expected_packet), packet->length,
                                         driver->name);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(expected_packet, packet->bytes,
                                             sizeof(expected_packet), driver->name);
        TEST_ASSERT_EQUAL_PTR(job,
                              bm13xx_harness_active_job(expected[index].job_id));
        bm13xx_harness_end();
    }
}

TEST_CASE("BM1397 work packet and returned midstate preserve version mapping",
          "[asic][job-packet][version-rolling][characterization]")
{
    /* SHA-256 states from OpenSSL SHA256_Update over each first header block;
     * frame CRC from Python binascii.crc_hqx, initial value 0xffff. */
    static const uint8_t expected_packet[] = {
        0x55, 0xaa, 0x21, 0x96, 0x04, 0x04, 0x78, 0x56,
        0x34, 0x12, 0x01, 0xdd, 0x05, 0x17, 0xd8, 0x8b,
        0x65, 0x64, 0x00, 0x01, 0x02, 0x03, 0x41, 0x70,
        0x41, 0x61, 0x2e, 0x05, 0x0d, 0xa2, 0x43, 0xd9,
        0x74, 0x92, 0x1b, 0xca, 0xaf, 0xe6, 0x98, 0x24,
        0x48, 0xd8, 0xb4, 0xe9, 0x69, 0xdd, 0xfd, 0xaf,
        0x43, 0x29, 0x34, 0x6f, 0x12, 0x2f, 0xb4, 0x45,
        0xa6, 0x48, 0x0f, 0x3b, 0xb4, 0x5f, 0x36, 0xc5,
        0x30, 0x09, 0xbb, 0x49, 0x95, 0x44, 0xe2, 0xb2,
        0x01, 0xa8, 0x6a, 0xe6, 0x53, 0x13, 0xc0, 0x4e,
        0x91, 0x04, 0x29, 0x90, 0x0e, 0x32, 0x94, 0xeb,
        0xcf, 0x87, 0xd2, 0x4d, 0x3f, 0x67, 0x9d, 0x0e,
        0x88, 0xf3, 0x82, 0x0c, 0xae, 0x49, 0xd5, 0xa4,
        0x0a, 0x8f, 0x37, 0x1b, 0x52, 0x3c, 0x9a, 0xfa,
        0x28, 0xc4, 0xed, 0xfc, 0xe7, 0x16, 0x58, 0xcf,
        0xd5, 0x38, 0xa5, 0x9c, 0x04, 0xf0, 0xdd, 0xe5,
        0x94, 0xe5, 0x75, 0x6b, 0x74, 0x3f, 0x94, 0x8d,
        0x51, 0x7e, 0xd9, 0x2b, 0x8b, 0xb2, 0x8a, 0x69,
        0x1d, 0xb9, 0x30, 0x0a, 0x1b, 0x40, 0x44, 0xc3,
    };
    static const uint32_t expected_versions[] = {
        0x20000004, 0x20002004, 0x20004004, 0x20006004,
    };

    GlobalState *state = bm1397_harness_begin();
    TEST_ASSERT_EQUAL_UINT8(2, bm1397_harness_driver.init(state));
    bm1397_harness_clear_packets();

    asic_job_t *job = make_job();
    bm1397_harness_driver.send_work(state, job);

    TEST_ASSERT_EQUAL_UINT32(1, bm1397_harness_packet_count());
    const bm1397_harness_packet_t *packet = bm1397_harness_packet(0);
    TEST_ASSERT_EQUAL_UINT32(sizeof(expected_packet), packet->length);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_packet, packet->bytes,
                                 sizeof(expected_packet));
    TEST_ASSERT_EQUAL_PTR(job, bm1397_harness_active_job(4));

    for (uint8_t index = 0; index < 4; ++index) {
        uint32_t nonce = 0x0a4c049b + index;
        queue_bm1397_job_response(4, index, nonce);
        const task_result *result = bm1397_harness_driver.process_work(state);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_EQUAL_STRING("packet-job", result->job.job_id);
        TEST_ASSERT_EQUAL_HEX32(nonce, result->nonce);
        TEST_ASSERT_EQUAL_HEX32(expected_versions[index], result->rolled_version);
        TEST_ASSERT_TRUE(result->timestamp_us == UINT64_C(123456789));
    }
    bm1397_harness_end();
}

TEST_CASE("Bitmain occupied work slots transfer ownership to replacements",
          "[asic][job-store][characterization]")
{
    static const uint8_t job_id_steps[] = {8, 24, 24, 24};

    for (size_t index = 0; index < BM13XX_HARNESS_DRIVER_COUNT; ++index) {
        const bm13xx_harness_driver_t *driver = &bm13xx_harness_drivers[index];
        GlobalState *state = bm13xx_harness_begin();

        asic_job_t *first = make_job();
        driver->send_work(state, first);
        uint8_t first_id = bm13xx_harness_packet(0)->bytes[4];
        uint8_t replacement_id =
            (uint8_t)((first_id + job_id_steps[index]) % 128);
        bm13xx_harness_install_job(replacement_id, make_job());

        asic_job_t *replacement = make_job();
        driver->send_work(state, replacement);

        TEST_ASSERT_EQUAL_HEX8_MESSAGE(
            replacement_id, bm13xx_harness_packet(1)->bytes[4], driver->name);
        TEST_ASSERT_EQUAL_PTR_MESSAGE(
            replacement, bm13xx_harness_active_job(replacement_id), driver->name);
        bm13xx_harness_end();
    }

    GlobalState *state = bm1397_harness_begin();
    TEST_ASSERT_EQUAL_UINT8(2, bm1397_harness_driver.init(state));
    bm1397_harness_clear_packets();

    asic_job_t *first = make_job();
    bm1397_harness_driver.send_work(state, first);
    uint8_t first_id = bm1397_harness_packet(0)->bytes[4];
    uint8_t replacement_id = (uint8_t)((first_id + 4) % 128);
    bm1397_harness_install_job(replacement_id, make_job());

    asic_job_t *replacement = make_job();
    bm1397_harness_driver.send_work(state, replacement);

    TEST_ASSERT_EQUAL_HEX8(replacement_id, bm1397_harness_packet(1)->bytes[4]);
    TEST_ASSERT_EQUAL_PTR(
        replacement, bm1397_harness_active_job(replacement_id));
    bm1397_harness_end();
}

TEST_CASE("BM1397 register results preserve type address value and reset job fields",
          "[asic][result][register][characterization]")
{
    GlobalState *state = bm1397_harness_begin();
    TEST_ASSERT_EQUAL_UINT8(2, bm1397_harness_driver.init(state));

    const uint8_t register_response[BM1397_HARNESS_RESPONSE_SIZE] = {
        0xaa, 0x55, 0x12, 0x34, 0x56, 0x78, 0x80, 0x4c, 0x00,
    };
    bm1397_harness_queue_response(register_response);
    const task_result *result = bm1397_harness_driver.process_work(state);

    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(REGISTER_ERROR_COUNT, result->register_type);
    TEST_ASSERT_EQUAL_UINT8(1, result->asic_nr);
    TEST_ASSERT_EQUAL_HEX32(0x12345678, result->value);
    const uint8_t empty_job[sizeof(asic_job_t)] = {0};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(empty_job, &result->job, sizeof(empty_job));
    TEST_ASSERT_EQUAL_HEX32(0, result->nonce);
    TEST_ASSERT_EQUAL_HEX32(0, result->rolled_version);
    TEST_ASSERT_TRUE(result->timestamp_us == UINT64_C(123456789));

    const uint8_t unknown_register[BM1397_HARNESS_RESPONSE_SIZE] = {
        0xaa, 0x55, 0, 0, 0, 0, 0, 0x01, 0x00,
    };
    bm1397_harness_queue_response(unknown_register);
    TEST_ASSERT_NULL(bm1397_harness_driver.process_work(state));
    bm1397_harness_end();
}

TEST_CASE("BM1397 rejects inactive job results and repeated nonces",
          "[asic][result][job-store][characterization]")
{
    GlobalState *state = bm1397_harness_begin();
    TEST_ASSERT_EQUAL_UINT8(2, bm1397_harness_driver.init(state));
    bm1397_harness_clear_packets();

    TEST_ASSERT_NULL(bm1397_harness_driver.process_work(state));
    queue_bm1397_job_response(0x7c, 0, 0x13579bdf);
    TEST_ASSERT_NULL(bm1397_harness_driver.process_work(state));
    bm1397_harness_mark_job_valid(0x78);
    queue_bm1397_job_response(0x78, 0, 0x13579be0);
    TEST_ASSERT_NULL(bm1397_harness_driver.process_work(state));

    asic_job_t *job = make_job();
    bm1397_harness_driver.send_work(state, job);
    uint8_t job_id = bm1397_harness_packet(0)->bytes[4];
    uint32_t nonce = 0x2468ace1;
    queue_bm1397_job_response(job_id, 2, nonce);
    const task_result *result = bm1397_harness_driver.process_work(state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("packet-job", result->job.job_id);
    TEST_ASSERT_EQUAL_HEX32(nonce, result->nonce);
    TEST_ASSERT_EQUAL_HEX32(0x20004004, result->rolled_version);

    queue_bm1397_job_response(job_id, 2, nonce);
    TEST_ASSERT_NULL(bm1397_harness_driver.process_work(state));
    bm1397_harness_end();
}

TEST_CASE("BM1397 sparse mask wrap keeps packet midstates and returned versions paired",
          "[asic][job-packet][version-rolling]")
{
    /* Independent OpenSSL SHA256_Update states for the fixture header. */
    static const char *expected_midstates[] = {
        "aea213142fce26501c1f89c22176d042157c1069d260391e164ddf1d7dfb99ce",
        "417041612e050da243d974921bcaafe6982448d8b4e969ddfdaf4329346f122f",
        "b445a6480f3bb45f36c53009bb499544e2b201a86ae65313c04e910429900e32",
        "a28a153db5776db19a0c70fe4544ff93ec146ca3fb92405f59adc55b0b454e54",
    };
    const uint32_t expected_versions[] = {
        0x3000a004, 0x20000004, 0x20002004, 0x20008004,
    };
    GlobalState *state = bm1397_harness_begin();
    TEST_ASSERT_EQUAL_UINT8(2, bm1397_harness_driver.init(state));
    bm1397_harness_clear_packets();
    asic_job_t *job = make_job();
    job->version = expected_versions[0];
    job->version_mask = 0x1000a000;
    bm1397_harness_driver.send_work(state, job);
    const bm1397_harness_packet_t *sent = bm1397_harness_packet(0);
    TEST_ASSERT_EQUAL_UINT32(152, sent->length);
    TEST_ASSERT_EQUAL_UINT8(4, sent->bytes[5]);
    uint8_t job_id = sent->bytes[4];
    for (uint8_t i = 0; i < BM1397_NUM_MIDSTATES; ++i) {
        uint8_t expected[32];
        TEST_ASSERT_EQUAL_UINT32(32, hex2bin(expected_midstates[i], expected, 32));
        TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, sent->bytes + 22 + i * 32, 32);
        queue_bm1397_job_response(job_id, i, 0x71000000 + i);
        const task_result *result = bm1397_harness_driver.process_work(state);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_EQUAL_HEX32(expected_versions[i], result->rolled_version);
    }
    bm1397_harness_end();
}

TEST_CASE("BM1397 disabled rolling sends one midstate and returns the base version",
          "[asic][job-packet][version-rolling]")
{
    GlobalState *state = bm1397_harness_begin();
    TEST_ASSERT_EQUAL_UINT8(2, bm1397_harness_driver.init(state));
    bm1397_harness_clear_packets();
    asic_job_t *job = make_job();
    job->version_mask = 0;
    bm1397_harness_driver.send_work(state, job);
    const bm1397_harness_packet_t *sent = bm1397_harness_packet(0);
    TEST_ASSERT_EQUAL_UINT32(152, sent->length);
    TEST_ASSERT_EQUAL_UINT8(1, sent->bytes[5]);
    uint8_t expected[32];
    TEST_ASSERT_EQUAL_UINT32(32, hex2bin(
        "417041612e050da243d974921bcaafe6982448d8b4e969ddfdaf4329346f122f", expected, 32));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, sent->bytes + 22, 32);
    const uint8_t unused[96] = {0};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(unused, sent->bytes + 54, sizeof(unused));
    queue_bm1397_job_response(sent->bytes[4], 0, 0x72000000);
    const task_result *result = bm1397_harness_driver.process_work(state);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_HEX32(0x20000004, result->rolled_version);
    bm1397_harness_end();
}
