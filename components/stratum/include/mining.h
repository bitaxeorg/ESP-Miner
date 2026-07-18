#ifndef MINING_H_
#define MINING_H_

#include "stratum_api.h"

typedef struct
{
    uint32_t version;
    uint32_t version_mask;
    uint8_t prev_block_hash[32];
    uint8_t merkle_root[32];
    uint32_t ntime;
    uint32_t target; // aka difficulty, aka nbits
    uint32_t starting_nonce;

    uint8_t num_midstates;
    uint8_t midstate[32];
    uint8_t midstate1[32];
    uint8_t midstate2[32];
    uint8_t midstate3[32];
    double pool_diff;
    char *jobid;
    char *extranonce2;
} bm_job;

void free_bm_job(bm_job *job);

void calculate_coinbase_tx_hash(const char *coinbase_1, const char *coinbase_2,
                                const char *extranonce, const char *extranonce_2, uint8_t dest[32]);

void calculate_coinbase_tx_hash_bin(const uint8_t *prefix, size_t prefix_len,
                                    const uint8_t *extranonce_prefix, size_t ep_len,
                                    const uint8_t *extranonce_2, size_t e2_len,
                                    const uint8_t *suffix, size_t suffix_len,
                                    uint8_t dest[32]);

void calculate_merkle_root_hash(const uint8_t coinbase_tx_hash[32], const uint8_t merkle_branches[][32], const int num_merkle_branches, uint8_t dest[32]);

void construct_bm_job(mining_notify *params, const uint8_t merkle_root[32], const uint32_t version_mask, const double difficulty, bm_job* new_job);

// Convert a 256-bit value (block hash or pool target, little-endian) to
// difficulty (pdiff = truediffone / value). Shared by SV1 (test_nonce_value)
// and SV2 (target). Returns a double to preserve fractional difficulty.
double hash_to_pdiff(const uint8_t hash[32]);

double test_nonce_value(const bm_job *job, const uint32_t nonce, const uint32_t rolled_version);

void extranonce_2_generate(uint64_t extranonce_2, uint32_t length, char dest[static length * 2 + 1]);

uint32_t increment_bitmask(const uint32_t value, const uint32_t mask);

#endif /* MINING_H_ */