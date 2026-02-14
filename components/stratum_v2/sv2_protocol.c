#include "sv2_api.h"
#include <string.h>
#include <math.h>

// --- Little-endian helpers ---

static inline uint16_t read_u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t read_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint64_t read_u64_le(const uint8_t *p)
{
    return (uint64_t)read_u32_le(p) | ((uint64_t)read_u32_le(p + 4) << 32);
}

static inline void write_u16_le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
}

static inline void write_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

// Write STR0_255: 1 byte length + string bytes. Returns bytes written.
static int write_str0255(uint8_t *dest, size_t dest_len, const char *str)
{
    size_t slen = str ? strlen(str) : 0;
    if (slen > 255) slen = 255;
    if (dest_len < 1 + slen) return -1;
    dest[0] = (uint8_t)slen;
    if (slen > 0) memcpy(dest + 1, str, slen);
    return 1 + (int)slen;
}

// Read STR0_255: returns number of bytes consumed, or -1 on error
static int read_str0255(const uint8_t *src, uint32_t src_len, char *dest, size_t dest_size)
{
    if (src_len < 1) return -1;
    uint8_t slen = src[0];
    if (src_len < 1u + slen) return -1;
    size_t copy = slen;
    if (copy >= dest_size) copy = dest_size - 1;
    memcpy(dest, src + 1, copy);
    dest[copy] = '\0';
    return 1 + slen;
}

// --- Frame header ---

int sv2_parse_frame_header(const uint8_t *data, sv2_frame_header_t *header)
{
    header->extension_type = read_u16_le(data);
    header->msg_type = data[2];
    // msg_length is 24-bit LE (bytes 3-5)
    header->msg_length = (uint32_t)data[3] | ((uint32_t)data[4] << 8) | ((uint32_t)data[5] << 16);
    return 0;
}

int sv2_encode_frame_header(uint8_t *dest, uint16_t extension_type, uint8_t msg_type, uint32_t msg_length)
{
    write_u16_le(dest, extension_type);
    dest[2] = msg_type;
    dest[3] = (uint8_t)(msg_length);
    dest[4] = (uint8_t)(msg_length >> 8);
    dest[5] = (uint8_t)(msg_length >> 16);
    return SV2_FRAME_HEADER_SIZE;
}

// --- Message builders ---

int sv2_build_setup_connection(uint8_t *buf, size_t buf_len,
                               const char *host, uint16_t port,
                               const char *vendor, const char *hw_version,
                               const char *firmware, const char *device_id)
{
    // Build payload first, then prepend header
    uint8_t payload[512];
    int pos = 0;

    // protocol: u8 = 0 (Mining Protocol)
    payload[pos++] = 0x00;

    // min_version: u16 LE = 2
    write_u16_le(payload + pos, 2);
    pos += 2;

    // max_version: u16 LE = 2
    write_u16_le(payload + pos, 2);
    pos += 2;

    // flags: u32 LE = 1 (REQUIRES_STANDARD_JOBS)
    write_u32_le(payload + pos, 0x01);
    pos += 4;

    // endpoint_host: STR0_255
    int n = write_str0255(payload + pos, sizeof(payload) - pos, host);
    if (n < 0) return -1;
    pos += n;

    // endpoint_port: u16 LE
    write_u16_le(payload + pos, port);
    pos += 2;

    // vendor: STR0_255
    n = write_str0255(payload + pos, sizeof(payload) - pos, vendor);
    if (n < 0) return -1;
    pos += n;

    // hardware_version: STR0_255
    n = write_str0255(payload + pos, sizeof(payload) - pos, hw_version);
    if (n < 0) return -1;
    pos += n;

    // firmware: STR0_255
    n = write_str0255(payload + pos, sizeof(payload) - pos, firmware);
    if (n < 0) return -1;
    pos += n;

    // device_id: STR0_255
    n = write_str0255(payload + pos, sizeof(payload) - pos, device_id);
    if (n < 0) return -1;
    pos += n;

    // Check output buffer size
    int total = SV2_FRAME_HEADER_SIZE + pos;
    if ((size_t)total > buf_len) return -1;

    // Write frame header (not a channel message)
    sv2_encode_frame_header(buf, 0x0000, SV2_MSG_SETUP_CONNECTION, (uint32_t)pos);

    // Copy payload
    memcpy(buf + SV2_FRAME_HEADER_SIZE, payload, pos);

    return total;
}

int sv2_build_open_standard_mining_channel(uint8_t *buf, size_t buf_len,
                                           uint32_t request_id,
                                           const char *user_identity,
                                           float nominal_hash_rate)
{
    uint8_t payload[512];
    int pos = 0;

    // request_id: u32 LE
    write_u32_le(payload + pos, request_id);
    pos += 4;

    // user_identity: STR0_255
    int n = write_str0255(payload + pos, sizeof(payload) - pos, user_identity);
    if (n < 0) return -1;
    pos += n;

    // nominal_hash_rate: f32 LE
    uint32_t f_bits;
    memcpy(&f_bits, &nominal_hash_rate, 4);
    write_u32_le(payload + pos, f_bits);
    pos += 4;

    // max_target: U256 (32 bytes, all 0xFF = max difficulty acceptance)
    memset(payload + pos, 0xFF, 32);
    pos += 32;

    int total = SV2_FRAME_HEADER_SIZE + pos;
    if ((size_t)total > buf_len) return -1;

    sv2_encode_frame_header(buf, 0x0000, SV2_MSG_OPEN_STANDARD_MINING_CHANNEL, (uint32_t)pos);
    memcpy(buf + SV2_FRAME_HEADER_SIZE, payload, pos);

    return total;
}

int sv2_build_submit_shares_standard(uint8_t *buf, size_t buf_len,
                                     uint32_t channel_id, uint32_t sequence_number,
                                     uint32_t job_id, uint32_t nonce,
                                     uint32_t ntime, uint32_t version)
{
    // Payload: 6 * u32 = 24 bytes
    int payload_len = 24;
    int total = SV2_FRAME_HEADER_SIZE + payload_len;
    if ((size_t)total > buf_len) return -1;

    // Channel message: extension_type has bit 15 set
    sv2_encode_frame_header(buf, SV2_CHANNEL_MSG_FLAG, SV2_MSG_SUBMIT_SHARES_STANDARD, (uint32_t)payload_len);

    uint8_t *p = buf + SV2_FRAME_HEADER_SIZE;
    write_u32_le(p, channel_id);      p += 4;
    write_u32_le(p, sequence_number); p += 4;
    write_u32_le(p, job_id);          p += 4;
    write_u32_le(p, nonce);           p += 4;
    write_u32_le(p, ntime);           p += 4;
    write_u32_le(p, version);

    return total;
}

// --- Message parsers ---

int sv2_parse_setup_connection_success(const uint8_t *payload, uint32_t len,
                                       uint16_t *used_version, uint32_t *flags)
{
    // used_version(u16) + flags(u32) = 6 bytes
    if (len < 6) return -1;
    *used_version = read_u16_le(payload);
    *flags = read_u32_le(payload + 2);
    return 0;
}

int sv2_parse_open_channel_success(const uint8_t *payload, uint32_t len,
                                   uint32_t *request_id, uint32_t *channel_id,
                                   uint8_t target[32],
                                   uint8_t *extranonce_prefix, uint8_t *extranonce_prefix_len,
                                   uint32_t *group_channel_id)
{
    // request_id(4) + channel_id(4) + target(32) + B0_32(1+N) + group_channel_id(4) = min 45 bytes
    if (len < 45) return -1;

    int pos = 0;
    *request_id = read_u32_le(payload + pos); pos += 4;
    *channel_id = read_u32_le(payload + pos); pos += 4;
    memcpy(target, payload + pos, 32); pos += 32;

    // B0_32: 1 byte length + data
    uint8_t prefix_len = payload[pos++];
    if (prefix_len > 32) return -1;
    if ((uint32_t)pos + prefix_len + 4 > len) return -1;
    *extranonce_prefix_len = prefix_len;
    if (prefix_len > 0) {
        memcpy(extranonce_prefix, payload + pos, prefix_len);
    }
    pos += prefix_len;

    *group_channel_id = read_u32_le(payload + pos);
    return 0;
}

int sv2_parse_new_mining_job(const uint8_t *payload, uint32_t len,
                             uint32_t *channel_id, uint32_t *job_id,
                             bool *has_min_ntime, uint32_t *min_ntime,
                             uint32_t *version,
                             uint8_t merkle_root[32])
{
    // min_ntime is Sv2Option<u32>: 1 byte flag + optional 4 byte value
    // Without min_ntime: channel_id(4) + job_id(4) + flag(1) + version(4) + merkle_root(32) = 45
    // With min_ntime:    channel_id(4) + job_id(4) + flag(1) + ntime(4) + version(4) + merkle_root(32) = 49
    if (len < 45) return -1;

    int pos = 0;
    *channel_id = read_u32_le(payload + pos); pos += 4;
    *job_id = read_u32_le(payload + pos); pos += 4;

    // Parse Sv2Option<u32> for min_ntime
    uint8_t option_flag = payload[pos]; pos += 1;
    if (option_flag == 0x01) {
        if (len < 49) return -1;
        *has_min_ntime = true;
        *min_ntime = read_u32_le(payload + pos); pos += 4;
    } else {
        *has_min_ntime = false;
        *min_ntime = 0;
    }

    *version = read_u32_le(payload + pos); pos += 4;
    memcpy(merkle_root, payload + pos, 32);
    return 0;
}

int sv2_parse_set_new_prev_hash(const uint8_t *payload, uint32_t len,
                                uint32_t *channel_id, uint32_t *job_id,
                                uint8_t prev_hash[32],
                                uint32_t *min_ntime, uint32_t *nbits)
{
    // channel_id(4) + job_id(4) + prev_hash(32) + min_ntime(4) + nbits(4) = 48 bytes
    if (len < 48) return -1;

    int pos = 0;
    *channel_id = read_u32_le(payload + pos); pos += 4;
    *job_id = read_u32_le(payload + pos); pos += 4;
    memcpy(prev_hash, payload + pos, 32); pos += 32;
    *min_ntime = read_u32_le(payload + pos); pos += 4;
    *nbits = read_u32_le(payload + pos);
    return 0;
}

int sv2_parse_set_target(const uint8_t *payload, uint32_t len,
                         uint32_t *channel_id, uint8_t max_target[32])
{
    // channel_id(4) + max_target(32) = 36 bytes
    if (len < 36) return -1;

    *channel_id = read_u32_le(payload);
    memcpy(max_target, payload + 4, 32);
    return 0;
}

int sv2_parse_submit_shares_success(const uint8_t *payload, uint32_t len,
                                    uint32_t *channel_id)
{
    // channel_id(4) + last_seq(4) + accepted_count(4) + shares_sum(8) = 20 bytes
    if (len < 20) return -1;
    *channel_id = read_u32_le(payload);
    return 0;
}

int sv2_parse_submit_shares_error(const uint8_t *payload, uint32_t len,
                                  uint32_t *channel_id, uint32_t *seq_num,
                                  char *error_code, size_t error_code_size)
{
    // channel_id(4) + seq_num(4) + STR0_255(1+N) = min 9 bytes
    if (len < 9) return -1;

    *channel_id = read_u32_le(payload);
    *seq_num = read_u32_le(payload + 4);

    int n = read_str0255(payload + 8, len - 8, error_code, error_code_size);
    if (n < 0) return -1;
    return 0;
}

// --- Helpers ---

// truediffone = 0x00000000FFFF0000000000000000000000000000000000000000000000000000
static const double truediffone = 26959535291011309493156476344723991336010898738574164086137773096960.0;
static const double bits192 = 6277101735386680763835789423207666416102355444464034512896.0;
static const double bits128 = 340282366920938463463374607431768211456.0;
static const double bits64 = 18446744073709551616.0;

// Convert LE 256-bit value to double (same logic as utils.c le256todouble)
static double le256_to_double(const uint8_t *target)
{
    double dcut64;
    uint64_t data64;

    memcpy(&data64, target + 24, 8);
    dcut64 = (double)data64 * bits192;

    memcpy(&data64, target + 16, 8);
    dcut64 += (double)data64 * bits128;

    memcpy(&data64, target + 8, 8);
    dcut64 += (double)data64 * bits64;

    memcpy(&data64, target, 8);
    dcut64 += (double)data64;

    return dcut64;
}

uint32_t sv2_target_to_pdiff(const uint8_t target[32])
{
    double target_d = le256_to_double(target);
    if (target_d == 0.0) return UINT32_MAX;
    double pdiff = truediffone / target_d;
    if (pdiff > (double)UINT32_MAX) return UINT32_MAX;
    if (pdiff < 1.0) return 1;
    return (uint32_t)pdiff;
}
