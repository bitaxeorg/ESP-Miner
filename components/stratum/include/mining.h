#ifndef MINING_H_
#define MINING_H_

#include <stdint.h>
#include <stddef.h>
#include "miner_job.h"
#include "utils.h"

#define BM_JOB_MAX_MIDSTATES 4

typedef struct bm_job
{
    uint32_t version;
    uint32_t version_mask;
    uint8_t prev_block_hash[32];
    uint8_t merkle_root[32];
    uint32_t ntime;
    uint32_t target; // aka difficulty, aka nbits
    uint32_t starting_nonce;

    uint8_t num_midstates;
    uint8_t midstates[BM_JOB_MAX_MIDSTATES][32];
    uint8_t pool_target[32];
    uint8_t network_target[32];
    uint8_t pool_id;
    miner_job_type_t job_type;
    char *jobid;
    char *extranonce2;
} bm_job;

void free_bm_job(bm_job *job);

void calculate_coinbase_tx_hash_bin(const uint8_t *prefix, size_t prefix_len,
                                    const uint8_t *extranonce_prefix, size_t ep_len,
                                    const uint8_t *extranonce_2, size_t e2_len,
                                    const uint8_t *suffix, size_t suffix_len,
                                    uint8_t dest[32]);

void calculate_merkle_root_hash(const uint8_t coinbase_tx_hash[32], const uint8_t merkle_branches[][32], const int num_merkle_branches, uint8_t dest[32]);

void construct_bm_job_from_miner_job(const miner_job_t *job, const uint32_t version, const uint8_t merkle_root[32], const uint32_t version_mask, const uint8_t software_midstates, bm_job *new_job);

void calculate_header_hash(const bm_job *job, const uint32_t nonce, const uint32_t rolled_version, uint8_t hash_result[32]);

uint32_t increment_bitmask(const uint32_t value, const uint32_t mask);

#endif /* MINING_H_ */
