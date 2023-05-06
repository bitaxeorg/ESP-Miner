#ifndef MINING_H
#define MINING_H

#include "stratum_api.h"

typedef struct {
    uint32_t starting_nonce;
    uint32_t target; // aka difficulty, aka nbits
    uint32_t ntime;
    uint32_t merkle_root_end;
    uint8_t midstate[32];
} bm_job;

char * construct_coinbase_tx(const char * coinbase_1, const char * coinbase_2,
                             const char * extranonce, int extranonce_2_len);

char * calculate_merkle_root_hash(const char * coinbase_tx, const uint8_t merkle_branches[][32], const int num_merkle_branches);

bm_job construct_bm_job(uint32_t version, const char * prev_block_hash, const char * merkle_root,
                        uint32_t ntime, uint32_t target);

#endif // MINING_H