#ifndef BZM_HASH_H
#define BZM_HASH_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "asic_job.h"


bool bzm_sha256(const uint8_t *data, size_t size, uint8_t digest[32]);
bool bzm_double_sha256(const uint8_t *data, size_t size, uint8_t digest[32]);
bool bzm_nonce_hash(const asic_job_t *job, uint32_t nonce, uint32_t version, uint8_t hash[32]);
bool bzm_hash_meets_difficulty(const uint8_t hash[32], uint8_t power);
static inline uint32_t bzm_increment_version(uint32_t value, uint32_t mask)
{
    return (value & ~mask) | (((value | ~mask) + 1U) & mask);
}

static inline void bzm_reverse_word_bytes(uint8_t data[32])
{
    for (size_t i = 0; i < 32; i += 4) {
        uint8_t a = data[i], b = data[i + 1];
        data[i] = data[i + 3]; data[i + 1] = data[i + 2];
        data[i + 2] = b; data[i + 3] = a;
    }
}
#endif
