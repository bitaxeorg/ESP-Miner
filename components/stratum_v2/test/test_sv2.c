#include "unity.h"
#include "sv2_protocol.h"
#include "sv2_noise.h"
#include "esp_heap_caps.h"
#include <sys/param.h>
#include <string.h>
#include <stdlib.h>

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

    // 5. Invalid case: extranonce_size < 2 (e.g. 0 and 1)
    payload[40] = 0; payload[41] = 0;
    TEST_ASSERT_EQUAL(-1, sv2_parse_open_extended_channel_success(
        payload, len_32, &req_id, &chan_id, target, &extranonce_size, prefix, &prefix_len, &group_id));
    payload[40] = 1; payload[41] = 0;
    TEST_ASSERT_EQUAL(-1, sv2_parse_open_extended_channel_success(
        payload, len_32, &req_id, &chan_id, target, &extranonce_size, prefix, &prefix_len, &group_id));

    // 6. Framing checks: trailing byte or truncated byte rejected
    payload[40] = 16; payload[41] = 0; // restore valid size
    TEST_ASSERT_EQUAL(-1, sv2_parse_open_extended_channel_success(
        payload, len_32 + 1, &req_id, &chan_id, target, &extranonce_size, prefix, &prefix_len, &group_id));
    TEST_ASSERT_EQUAL(-1, sv2_parse_open_extended_channel_success(
        payload, len_32 - 1, &req_id, &chan_id, target, &extranonce_size, prefix, &prefix_len, &group_id));
}

TEST_CASE("SV2 parse set extranonce prefix", "[sv2]")
{
    // Frame structure: channel_id(4) + B0_32(1+N)
    uint8_t payload[64];
    uint32_t chan_id;
    uint8_t prefix[32];
    uint8_t prefix_len;

    // 1. Valid case: channel_id = 7, prefix = 01 02 03 04
    memset(payload, 0, sizeof(payload));
    payload[0] = 7;
    payload[4] = 4;
    payload[5] = 1; payload[6] = 2; payload[7] = 3; payload[8] = 4;
    TEST_ASSERT_EQUAL(0, sv2_parse_set_extranonce_prefix(payload, 9, &chan_id, prefix, &prefix_len));
    TEST_ASSERT_EQUAL_UINT32(7, chan_id);
    TEST_ASSERT_EQUAL_UINT8(4, prefix_len);
    const uint8_t expected[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, prefix, 4);

    // 2. Empty prefix
    payload[4] = 0;
    TEST_ASSERT_EQUAL(0, sv2_parse_set_extranonce_prefix(payload, 5, &chan_id, prefix, &prefix_len));
    TEST_ASSERT_EQUAL_UINT8(0, prefix_len);

    // 3. Exact boundary: prefix_len = 32
    payload[4] = 32;
    TEST_ASSERT_EQUAL(0, sv2_parse_set_extranonce_prefix(payload, 5 + 32, &chan_id, prefix, &prefix_len));
    TEST_ASSERT_EQUAL_UINT8(32, prefix_len);

    // 4. Invalid case: prefix_len = 33 (> 32)
    payload[4] = 33;
    TEST_ASSERT_EQUAL(-1, sv2_parse_set_extranonce_prefix(payload, 5 + 33, &chan_id, prefix, &prefix_len));

    // 5. Framing checks: truncated prefix or trailing byte rejected
    payload[4] = 4;
    TEST_ASSERT_EQUAL(-1, sv2_parse_set_extranonce_prefix(payload, 8, &chan_id, prefix, &prefix_len));
    TEST_ASSERT_EQUAL(-1, sv2_parse_set_extranonce_prefix(payload, 10, &chan_id, prefix, &prefix_len));

    // 6. Payload shorter than channel_id + length byte
    TEST_ASSERT_EQUAL(-1, sv2_parse_set_extranonce_prefix(payload, 4, &chan_id, prefix, &prefix_len));
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

TEST_CASE("SV2 parse new extended mining job direct decoding", "[sv2]")
{
    uint8_t payload[256];
    memset(payload, 0, sizeof(payload));
    int pos = 0;

    // channel_id = 5
    payload[pos++] = 5; pos += 3;
    // job_id = 12
    payload[pos++] = 12; pos += 3;
    // min_ntime option flag = 1, min_ntime = 0x647025b5
    payload[pos++] = 1;
    payload[pos++] = 0xb5; payload[pos++] = 0x25; payload[pos++] = 0x70; payload[pos++] = 0x64;
    // version = 0x20000000
    payload[pos++] = 0x00; payload[pos++] = 0x00; payload[pos++] = 0x00; payload[pos++] = 0x20;
    // version_rolling_allowed = 1
    payload[pos++] = 1;
    // merkle_count = 1
    payload[pos++] = 1;
    memset(payload + pos, 0xab, 32);
    pos += 32;

    // prefix: len = 4, data = 0x01020304
    payload[pos++] = 4; payload[pos++] = 0;
    payload[pos++] = 1; payload[pos++] = 2; payload[pos++] = 3; payload[pos++] = 4;

    // suffix: len = 3, data = 0x050607
    payload[pos++] = 3; payload[pos++] = 0;
    payload[pos++] = 5; payload[pos++] = 6; payload[pos++] = 7;

    uint8_t p_buf[1024];
    miner_job_t job;
    memset(&job, 0, sizeof(job));
    job.coinbase_prefix = p_buf;

    uint32_t channel_id = 0;
    bool has_min_ntime = false;
    bool version_rolling_allowed = false;
    int res = sv2_parse_new_extended_mining_job(payload, pos, &channel_id, &job, &has_min_ntime, &version_rolling_allowed);

    TEST_ASSERT_EQUAL(0, res);
    TEST_ASSERT_EQUAL_UINT32(5, channel_id);
    TEST_ASSERT_TRUE(has_min_ntime);
    TEST_ASSERT_TRUE(version_rolling_allowed);
    TEST_ASSERT_EQUAL_STRING("12", job.job_id);
    TEST_ASSERT_EQUAL_HEX32(0x20000000, job.version);
    TEST_ASSERT_EQUAL_HEX32(0x647025b5, job.ntime);
    TEST_ASSERT_EQUAL(1, job.merkle_path_count);
    TEST_ASSERT_EQUAL_HEX8(0xab, job.merkle_path[0][0]);
    TEST_ASSERT_EQUAL(4, job.coinbase_prefix_len);
    TEST_ASSERT_EQUAL_HEX8(1, job.coinbase_prefix[0]);
    TEST_ASSERT_EQUAL_HEX8(4, job.coinbase_prefix[3]);
    TEST_ASSERT_EQUAL(3, job.coinbase_suffix_len);
    TEST_ASSERT_EQUAL_HEX8(5, job.coinbase_suffix[0]);
    TEST_ASSERT_EQUAL_HEX8(7, job.coinbase_suffix[2]);
    free(job.coinbase_suffix);
}

TEST_CASE("SV2 accepts a protocol-max coinbase suffix", "[sv2]")
{
    const size_t suffix_len = SV2_MAX_COINBASE_SUFFIX_LEN;
    const size_t payload_cap = suffix_len + 64;
    uint8_t *payload = malloc(payload_cap);
    TEST_ASSERT_NOT_NULL(payload);
    memset(payload, 0, payload_cap);

    int pos = 0;
    payload[pos++] = 5; pos += 3;               // channel_id = 5
    payload[pos++] = 12; pos += 3;              // job_id = 12
    payload[pos++] = 0;                         // future job, no min_ntime
    payload[pos++] = 0; payload[pos++] = 0; payload[pos++] = 0; payload[pos++] = 0x20; // version
    payload[pos++] = 1;                         // version rolling allowed
    payload[pos++] = 0;                         // merkle path count = 0
    payload[pos++] = 0; payload[pos++] = 0;     // prefix len = 0
    payload[pos++] = (uint8_t)(suffix_len & 0xff);
    payload[pos++] = (uint8_t)(suffix_len >> 8);
    memset(payload + pos, 0x5a, suffix_len);
    pos += (int)suffix_len;

    miner_job_t job;
    memset(&job, 0, sizeof(job));

    int res = sv2_parse_new_extended_mining_job(payload, (uint32_t)pos, NULL, &job, NULL, NULL);
    TEST_ASSERT_EQUAL(0, res);
    TEST_ASSERT_NOT_NULL(job.coinbase_suffix);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)suffix_len, job.coinbase_suffix_len);
    TEST_ASSERT_EQUAL_HEX8(0x5a, job.coinbase_suffix[0]);
    TEST_ASSERT_EQUAL_HEX8(0x5a, job.coinbase_suffix[suffix_len - 1]);

    // Truncated follow-up payload must fail without wiping the existing slot.
    TEST_ASSERT_EQUAL(-1, sv2_parse_new_extended_mining_job(payload, (uint32_t)pos - 1U, NULL, &job, NULL, NULL));
    TEST_ASSERT_EQUAL_STRING("12", job.job_id);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)suffix_len, job.coinbase_suffix_len);
    TEST_ASSERT_EQUAL_HEX8(0x5a, job.coinbase_suffix[suffix_len - 1]);

    free(payload);
    free(job.coinbase_suffix);
}

TEST_CASE("SV2 setup requires version rolling", "[sv2]")
{
    uint32_t standard_flags = sv2_setup_flags_for_channel(SV2_CHANNEL_STANDARD);
    uint32_t extended_flags = sv2_setup_flags_for_channel(SV2_CHANNEL_EXTENDED);

    TEST_ASSERT_BITS_HIGH(SV2_SETUP_FLAGS_REQUIRES_STANDARD_JOBS, standard_flags);
    TEST_ASSERT_BITS_HIGH(SV2_SETUP_FLAGS_REQUIRES_VERSION_ROLLING, standard_flags);
    TEST_ASSERT_BITS_LOW(SV2_SETUP_FLAGS_REQUIRES_STANDARD_JOBS, extended_flags);
    TEST_ASSERT_BITS_HIGH(SV2_SETUP_FLAGS_REQUIRES_VERSION_ROLLING, extended_flags);
    TEST_ASSERT_EQUAL_HEX32(0x01, SV2_SETUP_SUCCESS_FLAGS_REQUIRES_FIXED_VERSION);
    TEST_ASSERT_EQUAL_HEX32(0x02, SV2_SETUP_SUCCESS_FLAGS_REQUIRES_EXT_CHANNELS);
    TEST_ASSERT_TRUE(sv2_setup_success_allows_version_rolling(0));
    TEST_ASSERT_TRUE(sv2_setup_success_allows_version_rolling(
        SV2_SETUP_SUCCESS_FLAGS_REQUIRES_EXT_CHANNELS));
    TEST_ASSERT_FALSE(sv2_setup_success_allows_version_rolling(
        SV2_SETUP_SUCCESS_FLAGS_REQUIRES_FIXED_VERSION));
    TEST_ASSERT_FALSE(sv2_setup_success_allows_version_rolling(
        SV2_SETUP_SUCCESS_FLAGS_REQUIRES_FIXED_VERSION | SV2_SETUP_SUCCESS_FLAGS_REQUIRES_EXT_CHANNELS));
}

TEST_CASE("SV2 extended job reports fixed-version jobs", "[sv2]")
{
    uint8_t payload[256] = {0};
    int pos = 0;
    payload[pos++] = 5; pos += 3; // channel_id
    payload[pos++] = 12; pos += 3; // job_id
    payload[pos++] = 0; // min_ntime option flag = false
    payload[pos++] = 0x00; payload[pos++] = 0x00; payload[pos++] = 0x00; payload[pos++] = 0x20; // version
    payload[pos++] = 0; // version_rolling_allowed = false
    payload[pos++] = 0; // merkle_count = 0
    payload[pos++] = 0; payload[pos++] = 0; // prefix len = 0
    payload[pos++] = 0; payload[pos++] = 0; // suffix len = 0

    uint8_t p_buf[128];
    miner_job_t job;
    memset(&job, 0, sizeof(job));
    job.coinbase_prefix = p_buf;

    bool version_rolling_allowed = true;
    int res = sv2_parse_new_extended_mining_job(payload, pos, NULL, &job, NULL, &version_rolling_allowed);

    TEST_ASSERT_EQUAL(0, res);
    TEST_ASSERT_FALSE(version_rolling_allowed);
    TEST_ASSERT_EQUAL_HEX32(0, job.version_mask);
    free(job.coinbase_suffix);
}

TEST_CASE("SV2 accepts direct and group channel broadcasts", "[sv2]")
{
    TEST_ASSERT_TRUE(sv2_channel_or_group_matches(7, 7, 11));
    TEST_ASSERT_TRUE(sv2_channel_or_group_matches(11, 7, 11));
    TEST_ASSERT_FALSE(sv2_channel_or_group_matches(12, 7, 11));
    TEST_ASSERT_TRUE(sv2_channel_or_group_matches(7, 7, 0));
    TEST_ASSERT_FALSE(sv2_channel_or_group_matches(0, 7, 0));
}

TEST_CASE("SV2 share accounting keeps batched successes after one rejection", "[sv2]")
{
    uint32_t resolved = 0;

    // Ten accepted shares and one independent rejected share. The rejection
    // sequence position is not an absolute count of resolved shares.
    TEST_ASSERT_TRUE(sv2_account_rejected_share(11, &resolved, 10));
    TEST_ASSERT_EQUAL_UINT32(1, resolved);
    TEST_ASSERT_EQUAL_UINT32(10, sv2_account_accepted_shares(11, &resolved, 10));
    TEST_ASSERT_EQUAL_UINT32(11, resolved);

    // Repeated or excessive success count is bounded by pending shares.
    TEST_ASSERT_EQUAL_UINT32(0, sv2_account_accepted_shares(11, &resolved, 10));
    resolved = 2;
    TEST_ASSERT_EQUAL_UINT32(1, sv2_account_accepted_shares(3, &resolved, UINT32_MAX));
    TEST_ASSERT_EQUAL_UINT32(3, resolved);
}

TEST_CASE("SV2 error acknowledgement requires a submitted sequence number", "[sv2]")
{
    uint32_t resolved = 0;

    TEST_ASSERT_FALSE(sv2_account_rejected_share(0, &resolved, 0));
    TEST_ASSERT_FALSE(sv2_account_rejected_share(11, &resolved, 11));
    TEST_ASSERT_FALSE(sv2_account_rejected_share(11, &resolved, UINT32_MAX));
    TEST_ASSERT_EQUAL_UINT32(0, resolved);

    TEST_ASSERT_TRUE(sv2_account_rejected_share(11, &resolved, 10));
    TEST_ASSERT_EQUAL_UINT32(1, resolved);
    TEST_ASSERT_TRUE(sv2_account_rejected_share(11, &resolved, 0));
    TEST_ASSERT_EQUAL_UINT32(2, resolved);
}

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t write_pos;
    size_t read_pos;
    size_t max_read_chunk;
} sv2_loopback_transport_t;

static int sv2_loopback_write(esp_transport_handle_t transport, const char *buffer,
                              int len, int timeout_ms)
{
    (void)timeout_ms;
    sv2_loopback_transport_t *lb =
        (sv2_loopback_transport_t *)esp_transport_get_context_data(transport);
    if (!lb || len < 0 || lb->write_pos + (size_t)len > lb->cap) {
        return -1;
    }
    memcpy(lb->buf + lb->write_pos, buffer, (size_t)len);
    lb->write_pos += (size_t)len;
    return len;
}

static int sv2_loopback_read(esp_transport_handle_t transport, char *buffer,
                             int len, int timeout_ms)
{
    (void)timeout_ms;
    sv2_loopback_transport_t *lb =
        (sv2_loopback_transport_t *)esp_transport_get_context_data(transport);
    if (!lb || len <= 0 || lb->read_pos >= lb->write_pos) {
        return -1;
    }
    size_t avail = lb->write_pos - lb->read_pos;
    size_t to_copy = MIN((size_t)len, avail);
    if (lb->max_read_chunk > 0 && to_copy > lb->max_read_chunk) {
        to_copy = lb->max_read_chunk;
    }
    memcpy(buffer, lb->buf + lb->read_pos, to_copy);
    lb->read_pos += to_copy;
    return (int)to_copy;
}

TEST_CASE("SV2 Noise round-trip handles multi-chunk payloads over 65519 bytes", "[sv2]")
{
    const int payload_len = SV2_NOISE_MAX_PLAINTEXT_CHUNK_SIZE + 481; // 66,000 bytes (2 Noise chunks)
    const int frame_len = SV2_FRAME_HEADER_SIZE + payload_len;

    uint8_t *tx_frame = heap_caps_malloc(frame_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *rx_payload = heap_caps_malloc(SV2_MAX_RECV_FRAME_SIZE + SV2_NOISE_MAC_SIZE,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *wire_buf = heap_caps_malloc(72 * 1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    TEST_ASSERT_NOT_NULL(tx_frame);
    TEST_ASSERT_NOT_NULL(rx_payload);
    TEST_ASSERT_NOT_NULL(wire_buf);

    sv2_encode_frame_header(tx_frame, 0, SV2_MSG_NEW_EXTENDED_MINING_JOB, (uint32_t)payload_len);
    for (int i = 0; i < payload_len; i++) {
        tx_frame[SV2_FRAME_HEADER_SIZE + i] = (uint8_t)((i * 37 + 11) & 0xFF);
    }

    sv2_loopback_transport_t lb = {
        .buf = wire_buf,
        .cap = 72 * 1024,
        .write_pos = 0,
        .read_pos = 0,
        .max_read_chunk = 4093,
    };
    esp_transport_handle_t transport = esp_transport_init();
    TEST_ASSERT_NOT_NULL(transport);
    TEST_ASSERT_EQUAL(ESP_OK, esp_transport_set_context_data(transport, &lb));
    TEST_ASSERT_EQUAL(ESP_OK, esp_transport_set_func(transport, NULL, sv2_loopback_read,
                                                     sv2_loopback_write, NULL, NULL, NULL, NULL));

    uint8_t key_a[32];
    uint8_t key_b[32];
    for (int i = 0; i < 32; i++) {
        key_a[i] = (uint8_t)(0x10 + i);
        key_b[i] = (uint8_t)(0x80 + i);
    }

    sv2_noise_ctx_t *sender = sv2_noise_create();
    sv2_noise_ctx_t *receiver = sv2_noise_create();
    TEST_ASSERT_NOT_NULL(sender);
    TEST_ASSERT_NOT_NULL(receiver);
    TEST_ASSERT_EQUAL(0, sv2_noise_init_session_keys(sender, key_a, key_b));
    TEST_ASSERT_EQUAL(0, sv2_noise_init_session_keys(receiver, key_b, key_a));

    // Send a 66,000-byte frame (2 AEAD chunks) followed by a small frame to verify nonce sync
    uint8_t small_frame[SV2_FRAME_HEADER_SIZE + 8];
    sv2_encode_frame_header(small_frame, 0, SV2_MSG_SET_TARGET, 8);
    memcpy(small_frame + SV2_FRAME_HEADER_SIZE, "nextmsg!", 8);

    TEST_ASSERT_EQUAL(0, sv2_noise_send(sender, transport, tx_frame, frame_len));
    TEST_ASSERT_EQUAL(0, sv2_noise_send(sender, transport, small_frame, sizeof(small_frame)));

    uint8_t rx_hdr[SV2_FRAME_HEADER_SIZE] = {0};
    int rx_len = 0;
    TEST_ASSERT_EQUAL(0, sv2_noise_recv(receiver, transport, rx_hdr, rx_payload,
                                        SV2_MAX_RECV_FRAME_SIZE, &rx_len));
    TEST_ASSERT_EQUAL_INT(payload_len, rx_len);
    TEST_ASSERT_EQUAL_MEMORY(tx_frame, rx_hdr, SV2_FRAME_HEADER_SIZE);
    TEST_ASSERT_EQUAL_MEMORY(tx_frame + SV2_FRAME_HEADER_SIZE, rx_payload, payload_len);

    // Receive the follow-up small frame
    TEST_ASSERT_EQUAL(0, sv2_noise_recv(receiver, transport, rx_hdr, rx_payload,
                                        SV2_MAX_RECV_FRAME_SIZE, &rx_len));
    TEST_ASSERT_EQUAL_INT(8, rx_len);
    TEST_ASSERT_EQUAL_MEMORY(small_frame, rx_hdr, SV2_FRAME_HEADER_SIZE);
    TEST_ASSERT_EQUAL_MEMORY("nextmsg!", rx_payload, 8);

    sv2_noise_destroy(sender);
    sv2_noise_destroy(receiver);
    esp_transport_destroy(transport);
    free(tx_frame);
    free(rx_payload);
    free(wire_buf);
}
