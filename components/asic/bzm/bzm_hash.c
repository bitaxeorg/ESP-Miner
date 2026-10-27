#include "bzm/hash.h"
#include "bzm/platform.h"
#include <string.h>

static inline uint32_t sha256_rotr(uint32_t value, unsigned shift)
{
    return (value >> shift) | (value << (32 - shift));
}

static uint32_t sha256_load_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

static void sha256_store_be32(uint32_t value, uint8_t *dest)
{
    dest[0] = (uint8_t)(value >> 24);
    dest[1] = (uint8_t)(value >> 16);
    dest[2] = (uint8_t)(value >> 8);
    dest[3] = (uint8_t)value;
}

static const uint32_t sha256_round_constants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static const uint32_t sha256_initial_state[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

static void compress(uint32_t state[8], const uint8_t *data)
{
    uint32_t schedule[64];
    for (int i = 0; i < 16; i++) {
        schedule[i] = sha256_load_be32(data + (i * 4));
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = sha256_rotr(schedule[i - 15], 7) ^
                      sha256_rotr(schedule[i - 15], 18) ^
                      (schedule[i - 15] >> 3);
        uint32_t s1 = sha256_rotr(schedule[i - 2], 17) ^
                      sha256_rotr(schedule[i - 2], 19) ^
                      (schedule[i - 2] >> 10);
        schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t sum1 = sha256_rotr(e, 6) ^ sha256_rotr(e, 11) ^ sha256_rotr(e, 25);
        uint32_t choose = (e & f) ^ (~e & g);
        uint32_t temp1 = h + sum1 + choose + sha256_round_constants[i] + schedule[i];
        uint32_t sum0 = sha256_rotr(a, 2) ^ sha256_rotr(a, 13) ^ sha256_rotr(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = sum0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    const uint32_t working[8] = { a, b, c, d, e, f, g, h };
    for (int i = 0; i < 8; i++) {
        state[i] += working[i];
    }
}

bool bzm_sha256(const uint8_t *data, size_t size, uint8_t digest[32])
{
    if (digest == NULL || (data == NULL && size != 0)) return false;
#if SIZE_MAX > UINT64_MAX / 8U
    if (size > UINT64_MAX / 8U) return false;
#endif
    bzm_status_t accelerated = bzm_platform_sha256(data, size, digest);
    if (accelerated != BZM_ERR_NOT_SUPPORTED) return accelerated == BZM_OK;
    uint32_t state[8];
    memcpy(state, sha256_initial_state, sizeof(state));
    uint64_t bits = (uint64_t)size * 8U;
    while (size >= 64) {
        compress(state, data);
        data += 64;
        size -= 64;
    }
    uint8_t tail[128] = {0};
    if (size) memcpy(tail, data, size);
    tail[size] = 0x80;
    size_t padded = size < 56 ? 64 : 128;
    for (size_t i = 0; i < 8; ++i) tail[padded - 1 - i] = (uint8_t)(bits >> (8 * i));
    compress(state, tail);
    if (padded == 128) compress(state, tail + 64);
    for (size_t i = 0; i < 8; ++i) sha256_store_be32(state[i], digest + i * 4);
    return true;
}

bool bzm_double_sha256(const uint8_t *data, size_t size, uint8_t digest[32])
{
    uint8_t first[32];
    return bzm_sha256(data, size, first) && bzm_sha256(first, sizeof(first), digest);
}

bool bzm_nonce_hash(const asic_job_t *job, uint32_t nonce, uint32_t version, uint8_t hash[32])
{
    if (job == NULL) return false;
    uint8_t header[80];
    asic_job_header(job, nonce, version, header);
    return bzm_double_sha256(header, sizeof(header), hash);
}

bool bzm_hash_meets_difficulty(const uint8_t hash[32], uint8_t power)
{
    if (hash == NULL) return false;
    uint8_t target[32] = {[26] = 0xff, [27] = 0xff};
    for (unsigned bit = 0; bit < power; ++bit) {
        unsigned carry = 0;
        for (int i = 31; i >= 0; --i) {
            unsigned next = target[i] & 1U;
            target[i] = (uint8_t)((target[i] >> 1) | (carry << 7));
            carry = next;
        }
    }
    for (int i = 31; i >= 0; --i) {
        if (hash[i] < target[i]) return true;
        if (hash[i] > target[i]) return false;
    }
    return true;
}
