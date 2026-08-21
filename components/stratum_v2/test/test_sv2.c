#include "unity.h"
#include "sv2_protocol.h"
#include <string.h>

TEST_CASE("SV2 parse open extended channel extranonce bounds", "[sv2]")
{
    // Frame structure:
    // request_id(4) + channel_id(4) + target(32) + extranonce_size(2) + B0_32(1+N) + group_channel_id(4)
    uint8_t payload[128];
    uint32_t req_id, chan_id, group_id;
    uint8_t target[32];
    uint16_t extranonce_size;
    uint8_t prefix[32];
    uint8_t prefix_len;

    // 1. Valid case: prefix_len = 8, extranonce_size = 8 (total 16 <= 32)
    memset(payload, 0, sizeof(payload));
    int pos = 0;
    // req_id = 1, chan_id = 2
    payload[pos++] = 1; pos += 3;
    payload[pos++] = 2; pos += 3;
    pos += 32; // target
    payload[pos++] = 8; payload[pos++] = 0; // extranonce_size = 8
    payload[pos++] = 8; // prefix_len = 8
    pos += 8; // prefix bytes
    payload[pos++] = 3; pos += 3; // group_channel_id = 3

    TEST_ASSERT_EQUAL(0, sv2_parse_open_extended_channel_success(
        payload, pos, &req_id, &chan_id, target, &extranonce_size, prefix, &prefix_len, &group_id));
    TEST_ASSERT_EQUAL_UINT16(8, extranonce_size);
    TEST_ASSERT_EQUAL_UINT8(8, prefix_len);

    // 2. Invalid case: extranonce_size = 33 (> 32)
    payload[40] = 33; payload[41] = 0;
    TEST_ASSERT_EQUAL(-1, sv2_parse_open_extended_channel_success(
        payload, pos, &req_id, &chan_id, target, &extranonce_size, prefix, &prefix_len, &group_id));

    // 3. Invalid case: prefix_len = 20, extranonce_size = 16 (sum 36 > 32)
    payload[40] = 16; payload[41] = 0; // size = 16
    payload[42] = 20; // prefix_len = 20
    uint32_t len_36 = 40 + 2 + 1 + 20 + 4;
    TEST_ASSERT_EQUAL(-1, sv2_parse_open_extended_channel_success(
        payload, len_36, &req_id, &chan_id, target, &extranonce_size, prefix, &prefix_len, &group_id));

    // 4. Exact boundary case: prefix_len = 16, extranonce_size = 16 (sum 32)
    payload[40] = 16; payload[41] = 0; // size = 16
    payload[42] = 16; // prefix_len = 16
    uint32_t len_32 = 40 + 2 + 1 + 16 + 4;
    TEST_ASSERT_EQUAL(0, sv2_parse_open_extended_channel_success(
        payload, len_32, &req_id, &chan_id, target, &extranonce_size, prefix, &prefix_len, &group_id));
    TEST_ASSERT_EQUAL_UINT16(16, extranonce_size);
    TEST_ASSERT_EQUAL_UINT8(16, prefix_len);
}

TEST_CASE("SV2 parse submit shares error", "[sv2]")
{
    uint8_t payload[64];
    memset(payload, 0, sizeof(payload));
    payload[0] = 1; // channel_id = 1
    payload[4] = 42; // seq_num = 42
    payload[8] = 9; // strlen = 9
    memcpy(payload + 9, "stale-job", 9);

    uint32_t chan_id, seq;
    char error_code[32];
    TEST_ASSERT_EQUAL(0, sv2_parse_submit_shares_error(payload, 9 + 9, &chan_id, &seq, error_code, sizeof(error_code)));
    TEST_ASSERT_EQUAL_UINT32(1, chan_id);
    TEST_ASSERT_EQUAL_UINT32(42, seq);
    TEST_ASSERT_EQUAL_STRING("stale-job", error_code);
}
