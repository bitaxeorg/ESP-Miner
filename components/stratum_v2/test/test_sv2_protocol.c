#include <string.h>

#include "sv2_protocol.h"
#include "unity.h"

static size_t build_open_extended_success(uint8_t *payload,
                                          uint16_t extranonce_size,
                                          uint8_t prefix_len)
{
    const size_t payload_len = 47U + prefix_len;
    memset(payload, 0, payload_len);

    payload[40] = (uint8_t)(extranonce_size & 0xffU);
    payload[41] = (uint8_t)(extranonce_size >> 8U);
    payload[42] = prefix_len;
    for (uint8_t i = 0; i < prefix_len; i++) {
        payload[43U + i] = i;
    }

    return payload_len;
}

static int parse_open_extended_success(const uint8_t *payload, size_t payload_len)
{
    uint32_t request_id = 0;
    uint32_t channel_id = 0;
    uint8_t target[32] = {0};
    uint16_t extranonce_size = 0;
    uint8_t extranonce_prefix[32] = {0};
    uint8_t extranonce_prefix_len = 0;
    uint32_t group_channel_id = 0;

    return sv2_parse_open_extended_channel_success(
        payload, (uint32_t)payload_len, &request_id, &channel_id, target,
        &extranonce_size, extranonce_prefix, &extranonce_prefix_len,
        &group_channel_id);
}

TEST_CASE("SV2 extended channel accepts supported extranonce sizes", "[sv2]")
{
    uint8_t payload[47 + 32];

    size_t len = build_open_extended_success(payload, SV2_MIN_EXTRANONCE_SIZE, 0);
    TEST_ASSERT_EQUAL_INT(0, parse_open_extended_success(payload, len));

    len = build_open_extended_success(payload, SV2_MAX_EXTRANONCE_SIZE, 32);
    TEST_ASSERT_EQUAL_INT(0, parse_open_extended_success(payload, len));
}

TEST_CASE("SV2 extended channel rejects unsafe extranonce sizes", "[sv2]")
{
    static const uint16_t unsafe_sizes[] = {
        0, 1, 33, 255, 256, UINT16_MAX,
    };
    uint8_t payload[47];

    for (size_t i = 0; i < sizeof(unsafe_sizes) / sizeof(unsafe_sizes[0]); i++) {
        size_t len = build_open_extended_success(payload, unsafe_sizes[i], 0);
        TEST_ASSERT_EQUAL_INT(-1, parse_open_extended_success(payload, len));
    }
}

TEST_CASE("SV2 extended channel rejects invalid prefix framing", "[sv2]")
{
    uint8_t payload[47 + 33 + 1];
    size_t len = build_open_extended_success(payload, SV2_MIN_EXTRANONCE_SIZE, 33);
    TEST_ASSERT_EQUAL_INT(-1, parse_open_extended_success(payload, len));

    len = build_open_extended_success(payload, SV2_MIN_EXTRANONCE_SIZE, 0);
    TEST_ASSERT_EQUAL_INT(-1, parse_open_extended_success(payload, len - 1U));

    payload[len] = 0;
    TEST_ASSERT_EQUAL_INT(-1, parse_open_extended_success(payload, len + 1U));
}

TEST_CASE("SV2 extended channel request enforces local extranonce limits", "[sv2]")
{
    uint8_t frame[128];

    TEST_ASSERT_GREATER_THAN(0, sv2_build_open_extended_mining_channel(
                                    frame, sizeof(frame), 1, "miner", 1e12f,
                                    SV2_MIN_EXTRANONCE_SIZE));
    TEST_ASSERT_GREATER_THAN(0, sv2_build_open_extended_mining_channel(
                                    frame, sizeof(frame), 1, "miner", 1e12f,
                                    SV2_MAX_EXTRANONCE_SIZE));
    TEST_ASSERT_EQUAL_INT(-1, sv2_build_open_extended_mining_channel(
                                  frame, sizeof(frame), 1, "miner", 1e12f, 0));
    TEST_ASSERT_EQUAL_INT(-1, sv2_build_open_extended_mining_channel(
                                  frame, sizeof(frame), 1, "miner", 1e12f, 1));
    TEST_ASSERT_EQUAL_INT(-1, sv2_build_open_extended_mining_channel(
                                  frame, sizeof(frame), 1, "miner", 1e12f, 33));
    TEST_ASSERT_EQUAL_INT(-1, sv2_build_open_extended_mining_channel(
                                  frame, sizeof(frame), 1, "miner", 1e12f,
                                  UINT16_MAX));
}
