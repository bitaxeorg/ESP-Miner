#include "bm_job_midstate.h"
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

void midstate_sha256_bin(const uint8_t *data, const size_t data_len, uint8_t dest[32])
{
    if (data == NULL || data_len != 64) {
        memset(dest, 0, 32);
        return;
    }

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

    uint32_t a = sha256_initial_state[0];
    uint32_t b = sha256_initial_state[1];
    uint32_t c = sha256_initial_state[2];
    uint32_t d = sha256_initial_state[3];
    uint32_t e = sha256_initial_state[4];
    uint32_t f = sha256_initial_state[5];
    uint32_t g = sha256_initial_state[6];
    uint32_t h = sha256_initial_state[7];

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
        sha256_store_be32(sha256_initial_state[i] + working[i], dest + i * 4);
    }
}
