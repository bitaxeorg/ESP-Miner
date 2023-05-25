#ifndef MINING_H
#define MINING_H

#include "stratum_api.h"

typedef struct {
    uint32_t version;
    uint8_t prev_block_hash[32];
    uint8_t merkle_root[32];
    uint32_t ntime;
    uint32_t target; // aka difficulty, aka nbits
    uint32_t starting_nonce;

    uint8_t midstate[32];
    char * jobid;
    char * extranonce2;
} bm_job;

void free_bm_job(bm_job * job);

char * construct_coinbase_tx(const char * coinbase_1, const char * coinbase_2,
                             const char * extranonce, const char * extranonce_2);

char * calculate_merkle_root_hash(const char * coinbase_tx, const uint8_t merkle_branches[][32], const int num_merkle_branches);

bm_job construct_bm_job(mining_notify * params, const char * merkle_root);

double test_nonce_value(bm_job * job, uint32_t nonce);

char * extranonce_2_generate(uint32_t extranonce_2, uint32_t length);

#endif // MINING_H