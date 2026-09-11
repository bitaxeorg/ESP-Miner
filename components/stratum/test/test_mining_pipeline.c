#include "unity.h"

#include "mining.h"
#include "stratum_api.h"
#include "sv2_protocol.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>

/*
 * Golden characterization fixtures for the boundary that exists before the
 * ASIC refactor: Stratum input -> miner_job_t -> bm_job.  bm_job is currently
 * the value consumed by every ASIC_send_work implementation.
 *
 * The SV1 fixture, merkle root, and base-version midstate are also documented
 * by components/stratum/test/verifiers/bm1397.py. The additional midstates
 * lock the existing BIP320 version-roll order.
 */
static const char *SV1_NOTIFY_FIXTURE =
    "{\"id\":null,\"method\":\"mining.notify\",\"params\":["
    "\"1f9a56282c\","
    "\"bf44fd3513dc7b837d60e5c628b572b448d204a8000007490000000000000000\","
    "\"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b03e60e0cfabe6d6d7595fc426909f3a63c563a88773a618ec42cc51188ed0632b69f1c3053a8f8180100000000000000\","
    "\"2c28569a1f2f736c7573682f0000000003a1f3a22b000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a799d4c611eff5765ba06d2c58ad71b5734d677cea10942664b2a712d005108ae0000000000000000266a24aa21a9ed5c4d2056e3eef09b05d95897adec38c5c3f460a919e95f87e15664957c70305a00000000\","
    "["
    "\"4ea53a030256c37391b891b0d5060537df63944ce3fcd45121215596376bb3db\","
    "\"22cd1dde2c1b083237bbadd62ed1d51ee455265b7defe04dc8bcae7e5acacb33\","
    "\"60c781a8b02c07544cb3a91de3b4d7a13f9939c8579f3ac92fa28e802ace1b39\","
    "\"d89820b36568adc0705d71d639e69ccb7c168a1051697846cf5d98e5725ee4e3\","
    "\"73f0f773a3b6097388984f934ba1b01afc771c33db6df126cd6971cfea9f8f49\","
    "\"420958bbb39f6b8ad30e5b45b38a3825bf76f619b7dbb73a0366605ff882e91d\","
    "\"75f9ef87931104db956c88d65198596049af51017af4685c4548f2c31ec75b6d\","
    "\"70dd7189d5b927ac10a750062e5ab9f8b83fb784068e1c80d0df919bcf22e1b2\","
    "\"b34f2440b2b4609e44594885a397086339f4a2d880fb2d50ac585f757b895832\","
    "\"4c62d861fb259a743d1e2787eeac5bdd22a9883b5cc0b025843cff9441ea6b74\","
    "\"62522d5d8e2ff9d721a9a4b91931ec61069fff7c8ad23119718c068a035b9b1b\","
    "\"a0e7cf5509d9d0d87ff9a4f6332f76a243de01f4e93289b290e937e7fd03224f\""
    "],\"20000004\",\"1705dd01\",\"64658bd8\",true]}";

static void write_u32_le(uint8_t dest[4], uint32_t value)
{
    dest[0] = (uint8_t)value;
    dest[1] = (uint8_t)(value >> 8);
    dest[2] = (uint8_t)(value >> 16);
    dest[3] = (uint8_t)(value >> 24);
}

static void assert_hex32(const char *expected_hex, const uint8_t actual[32])
{
    uint8_t expected[32];
    TEST_ASSERT_EQUAL_UINT32(sizeof(expected),
                             hex2bin(expected_hex, expected, sizeof(expected)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, sizeof(expected));
}

static void assert_common_asic_job(const bm_job *job, miner_job_type_t type,
                                   uint8_t pool_id, double pool_diff,
                                   const char *expected_merkle_root)
{
    TEST_ASSERT_EQUAL_HEX32(0x20000004, job->version);
    TEST_ASSERT_EQUAL_HEX32(0x1fffe000, job->version_mask);
    TEST_ASSERT_EQUAL_HEX32(0x1705dd01, job->target);
    TEST_ASSERT_EQUAL_HEX32(0x64658bd8, job->ntime);
    TEST_ASSERT_EQUAL_UINT32(0, job->starting_nonce);
    TEST_ASSERT_EQUAL_UINT8(pool_id, job->pool_id);
    TEST_ASSERT_EQUAL_INT(type, job->job_type);
    TEST_ASSERT_EQUAL_DOUBLE(pool_diff, job->pool_diff);
    assert_hex32(
        "000000000000000049070000a804d248b472b528c6e5607d837bdc1335fd44bf",
        job->prev_block_hash);
    assert_hex32(expected_merkle_root, job->merkle_root);
}

static void parse_sv2_frame(const uint8_t *frame, size_t frame_size,
                            uint8_t expected_type, const uint8_t **payload,
                            uint32_t *payload_size)
{
    sv2_frame_header_t header = {0};

    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(SV2_FRAME_HEADER_SIZE, frame_size);
    TEST_ASSERT_EQUAL_INT(0, sv2_parse_frame_header(frame, &header));
    TEST_ASSERT_EQUAL_HEX16(SV2_CHANNEL_MSG_FLAG, header.extension_type);
    TEST_ASSERT_EQUAL_HEX8(expected_type, header.msg_type);
    TEST_ASSERT_EQUAL_UINT32(frame_size - SV2_FRAME_HEADER_SIZE, header.msg_length);

    *payload = frame + SV2_FRAME_HEADER_SIZE;
    *payload_size = header.msg_length;
}

TEST_CASE("SV1 notify reaches the ASIC job boundary byte exact",
          "[stratum][mining][characterization]")
{
    miner_job_pool_init();
    miner_job_t *miner_job = miner_job_get_slot(0);
    StratumApiV1Message message = {0};

    TEST_ASSERT_TRUE(STRATUM_V1_parse(&message, SV1_NOTIFY_FIXTURE, miner_job));
    TEST_ASSERT_EQUAL_INT(MINING_NOTIFY, message.method);
    TEST_ASSERT_EQUAL_PTR(miner_job, message.job);
    TEST_ASSERT_EQUAL_INT(JOB_TYPE_V1, miner_job->type);
    TEST_ASSERT_EQUAL_STRING("1f9a56282c", miner_job->job_id);
    TEST_ASSERT_TRUE(miner_job->clean_jobs);
    TEST_ASSERT_EQUAL_UINT8(12, miner_job->merkle_path_count);

    miner_job->extranonce1_len = (uint8_t)hex2bin(
        "1165060344b679", miner_job->extranonce1,
        sizeof(miner_job->extranonce1));
    TEST_ASSERT_EQUAL_UINT8(7, miner_job->extranonce1_len);
    miner_job->extranonce2_len = 8;
    miner_job->pool_id = 3;
    miner_job->pool_diff = 2048.0;
    miner_job->version_mask = BIP320_VERSION_ROLLING_MASK;

    uint8_t extranonce2[8] = {0};
    uint8_t coinbase_hash[32];
    calculate_coinbase_tx_hash_bin(
        miner_job->coinbase_prefix, miner_job->coinbase_prefix_len,
        miner_job->extranonce1, miner_job->extranonce1_len,
        extranonce2, sizeof(extranonce2),
        miner_job->coinbase_suffix, miner_job->coinbase_suffix_len,
        coinbase_hash);

    uint8_t merkle_root[32];
    calculate_merkle_root_hash(
        coinbase_hash, (const uint8_t (*)[32])miner_job->merkle_path,
        miner_job->merkle_path_count, merkle_root);
    assert_hex32(
        "35fd44bf837bdc13c6e5607db472b528a804d248490700000000000000000000",
        miner_job->prev_hash);
    assert_hex32(
        "cd1be82132ef0d12053dcece1fa0247fcfdb61d4dbd3eb32ea9ef9b4c604a846",
        merkle_root);

    bm_job asic_job = {0};
    construct_bm_job_from_miner_job(
        miner_job, miner_job->version, merkle_root, miner_job->version_mask,
        miner_job->pool_diff, 4, &asic_job);

    assert_common_asic_job(
        &asic_job, JOB_TYPE_V1, 3, 2048.0,
        "c604a846ea9ef9b4dbd3eb32cfdb61d41fa0247f053dcece32ef0d12cd1be821");
    TEST_ASSERT_EQUAL_UINT8(4, asic_job.num_midstates);
    assert_hex32(
        "4d3185f25f7d5de0e3591741cb21cae11574ddd65d490d3d68739f8a52eadf91",
        asic_job.midstates[0]);
    assert_hex32(
        "1223a956e9ef56cb49d27f0829c7afead0908ead93772919d4bc33efcb699658",
        asic_job.midstates[1]);
    assert_hex32(
        "4176317ef2641293a3effca4188675380c97daa32f37d00d3228f08966c0b97d",
        asic_job.midstates[2]);
    assert_hex32(
        "73828ae1e589678bc3f03b5a863835599cd73c42e3b67bbdc47f5b51eaec465a",
        asic_job.midstates[3]);

    STRATUM_V1_reset_message(&message);
}

TEST_CASE("SV2 standard messages reach the ASIC job boundary byte exact",
          "[sv2][mining][characterization]")
{
    uint8_t new_job_frame[SV2_FRAME_HEADER_SIZE + 45] = {
        0x00, 0x80, SV2_MSG_NEW_MINING_JOB, 45, 0x00, 0x00,
    };
    uint8_t *new_job_payload = new_job_frame + SV2_FRAME_HEADER_SIZE;
    write_u32_le(new_job_payload, 0x11223344);
    write_u32_le(new_job_payload + 4, 42);
    new_job_payload[8] = 0;
    write_u32_le(new_job_payload + 9, 0x20000004);
    TEST_ASSERT_EQUAL_UINT32(
        32, hex2bin(
                "cd1be82132ef0d12053dcece1fa0247fcfdb61d4dbd3eb32ea9ef9b4c604a846",
                new_job_payload + 13, 32));

    const uint8_t *payload = NULL;
    uint32_t payload_size = 0;
    parse_sv2_frame(new_job_frame, sizeof(new_job_frame),
                    SV2_MSG_NEW_MINING_JOB, &payload, &payload_size);

    uint32_t channel_id = 0;
    uint32_t job_id = 0;
    uint32_t min_ntime = 0;
    uint32_t version = 0;
    uint8_t merkle_root[32] = {0};
    bool has_min_ntime = false;
    TEST_ASSERT_EQUAL_INT(
        0, sv2_parse_new_mining_job(
               payload, payload_size, &channel_id, &job_id,
               &has_min_ntime, &min_ntime, &version, merkle_root));
    TEST_ASSERT_EQUAL_HEX32(0x11223344, channel_id);
    TEST_ASSERT_EQUAL_UINT32(42, job_id);
    TEST_ASSERT_FALSE(has_min_ntime);

    uint8_t prev_hash_frame[SV2_FRAME_HEADER_SIZE + 48] = {
        0x00, 0x80, SV2_MSG_SET_NEW_PREV_HASH, 48, 0x00, 0x00,
    };
    uint8_t *prev_hash_payload = prev_hash_frame + SV2_FRAME_HEADER_SIZE;
    write_u32_le(prev_hash_payload, channel_id);
    write_u32_le(prev_hash_payload + 4, job_id);
    TEST_ASSERT_EQUAL_UINT32(
        32, hex2bin(
                "35fd44bf837bdc13c6e5607db472b528a804d248490700000000000000000000",
                prev_hash_payload + 8, 32));
    write_u32_le(prev_hash_payload + 40, 0x64658bd8);
    write_u32_le(prev_hash_payload + 44, 0x1705dd01);

    parse_sv2_frame(prev_hash_frame, sizeof(prev_hash_frame),
                    SV2_MSG_SET_NEW_PREV_HASH, &payload, &payload_size);
    uint32_t prev_channel_id = 0;
    uint32_t prev_job_id = 0;
    uint32_t nbits = 0;
    uint8_t prev_hash[32] = {0};
    TEST_ASSERT_EQUAL_INT(
        0, sv2_parse_set_new_prev_hash(
               payload, payload_size, &prev_channel_id, &prev_job_id,
               prev_hash, &min_ntime, &nbits));
    TEST_ASSERT_EQUAL_HEX32(channel_id, prev_channel_id);
    TEST_ASSERT_EQUAL_UINT32(job_id, prev_job_id);

    miner_job_t miner_job = {
        .type = JOB_TYPE_SV2_STANDARD,
        .version = version,
        .ntime = min_ntime,
        .nbits = nbits,
        .clean_jobs = true,
        .pool_diff = 2048.0,
        .version_mask = BIP320_VERSION_ROLLING_MASK,
        .pool_id = 3,
    };
    (void)snprintf(miner_job.job_id, sizeof(miner_job.job_id), "%lu",
                   (unsigned long)job_id);
    memcpy(miner_job.prev_hash, prev_hash, sizeof(miner_job.prev_hash));
    memcpy(miner_job.merkle_root, merkle_root, sizeof(miner_job.merkle_root));

    bm_job asic_job = {0};
    construct_bm_job_from_miner_job(
        &miner_job, miner_job.version, miner_job.merkle_root,
        miner_job.version_mask, miner_job.pool_diff, 0, &asic_job);

    assert_common_asic_job(
        &asic_job, JOB_TYPE_SV2_STANDARD, 3, 2048.0,
        "c604a846ea9ef9b4dbd3eb32cfdb61d41fa0247f053dcece32ef0d12cd1be821");
    TEST_ASSERT_EQUAL_UINT8(0, asic_job.num_midstates);
    uint8_t zero_midstates[sizeof(asic_job.midstates)] = {0};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        zero_midstates, asic_job.midstates, sizeof(zero_midstates));
}

TEST_CASE("SV2 extended messages roll extranonce into the ASIC job byte exact",
          "[sv2][mining][characterization]")
{
    uint8_t new_job_frame[SV2_FRAME_HEADER_SIZE + 23] = {
        0x00, 0x80, SV2_MSG_NEW_EXTENDED_MINING_JOB, 23, 0x00, 0x00,
    };
    uint8_t *new_job_payload = new_job_frame + SV2_FRAME_HEADER_SIZE;
    size_t position = 0;
    write_u32_le(new_job_payload + position, 0x11223344); position += 4;
    write_u32_le(new_job_payload + position, 43); position += 4;
    new_job_payload[position++] = 0;
    write_u32_le(new_job_payload + position, 0x20000004); position += 4;
    new_job_payload[position++] = 1;
    new_job_payload[position++] = 0;
    new_job_payload[position++] = 2; new_job_payload[position++] = 0;
    new_job_payload[position++] = 0x01; new_job_payload[position++] = 0x02;
    new_job_payload[position++] = 2; new_job_payload[position++] = 0;
    new_job_payload[position++] = 0xfe; new_job_payload[position++] = 0xff;
    TEST_ASSERT_EQUAL_UINT32(23, position);

    const uint8_t *payload = NULL;
    uint32_t payload_size = 0;
    parse_sv2_frame(new_job_frame, sizeof(new_job_frame),
                    SV2_MSG_NEW_EXTENDED_MINING_JOB, &payload, &payload_size);

    miner_job_pool_init();
    miner_job_t *miner_job = miner_job_get_slot(1);
    uint32_t channel_id = 0;
    bool has_min_ntime = false;
    bool version_rolling_allowed = false;
    TEST_ASSERT_EQUAL_INT(
        0, sv2_parse_new_extended_mining_job(
               payload, payload_size, &channel_id, miner_job,
               &has_min_ntime, &version_rolling_allowed));
    TEST_ASSERT_EQUAL_HEX32(0x11223344, channel_id);
    TEST_ASSERT_FALSE(has_min_ntime);
    TEST_ASSERT_TRUE(version_rolling_allowed);
    TEST_ASSERT_EQUAL_INT(JOB_TYPE_SV2_EXTENDED, miner_job->type);
    TEST_ASSERT_EQUAL_STRING("43", miner_job->job_id);

    uint8_t prev_hash_frame[SV2_FRAME_HEADER_SIZE + 48] = {
        0x00, 0x80, SV2_MSG_SET_NEW_PREV_HASH, 48, 0x00, 0x00,
    };
    uint8_t *prev_hash_payload = prev_hash_frame + SV2_FRAME_HEADER_SIZE;
    write_u32_le(prev_hash_payload, channel_id);
    write_u32_le(prev_hash_payload + 4, 43);
    TEST_ASSERT_EQUAL_UINT32(
        32, hex2bin(
                "35fd44bf837bdc13c6e5607db472b528a804d248490700000000000000000000",
                prev_hash_payload + 8, 32));
    write_u32_le(prev_hash_payload + 40, 0x64658bd8);
    write_u32_le(prev_hash_payload + 44, 0x1705dd01);

    parse_sv2_frame(prev_hash_frame, sizeof(prev_hash_frame),
                    SV2_MSG_SET_NEW_PREV_HASH, &payload, &payload_size);
    uint32_t prev_channel_id = 0;
    uint32_t prev_job_id = 0;
    TEST_ASSERT_EQUAL_INT(
        0, sv2_parse_set_new_prev_hash(
               payload, payload_size, &prev_channel_id, &prev_job_id,
               miner_job->prev_hash, &miner_job->ntime, &miner_job->nbits));
    TEST_ASSERT_EQUAL_HEX32(channel_id, prev_channel_id);
    TEST_ASSERT_EQUAL_UINT32(43, prev_job_id);

    miner_job->clean_jobs = true;
    miner_job->pool_diff = 1024.0;
    miner_job->version_mask = BIP320_VERSION_ROLLING_MASK;
    miner_job->pool_id = 4;
    miner_job->extranonce1[0] = 0xaa;
    miner_job->extranonce1[1] = 0xbb;
    miner_job->extranonce1_len = 2;
    miner_job->extranonce2_len = 8;

    const uint8_t extranonce2[8] = {1, 0, 0, 0, 0, 0, 0, 0};
    uint8_t coinbase_hash[32];
    calculate_coinbase_tx_hash_bin(
        miner_job->coinbase_prefix, miner_job->coinbase_prefix_len,
        miner_job->extranonce1, miner_job->extranonce1_len,
        extranonce2, sizeof(extranonce2),
        miner_job->coinbase_suffix, miner_job->coinbase_suffix_len,
        coinbase_hash);
    uint8_t merkle_root[32];
    calculate_merkle_root_hash(
        coinbase_hash, (const uint8_t (*)[32])miner_job->merkle_path,
        miner_job->merkle_path_count, merkle_root);
    assert_hex32(
        "e706f70809a9b74cc63791d3cdd4af1801100fb9bbb6e222fa2fbcd06ad7452e",
        merkle_root);

    bm_job asic_job = {0};
    construct_bm_job_from_miner_job(
        miner_job, miner_job->version, merkle_root, miner_job->version_mask,
        miner_job->pool_diff, 0, &asic_job);

    assert_common_asic_job(
        &asic_job, JOB_TYPE_SV2_EXTENDED, 4, 1024.0,
        "6ad7452efa2fbcd0bbb6e22201100fb9cdd4af18c63791d309a9b74ce706f708");
    TEST_ASSERT_EQUAL_UINT8(0, asic_job.num_midstates);
}
