#ifndef MINING_H_
#define MINING_H_

#include <stdint.h>
#include <stddef.h>
#include "miner_job.h"

/* A zero version selects source->version. Destination is unchanged on failure. */
bool mining_build_asic_job(const miner_job_t *source, uint64_t extranonce2,
                           uint32_t version, asic_job_t *destination);

void calculate_coinbase_tx_hash_bin(const uint8_t *prefix, size_t prefix_len,
                                    const uint8_t *extranonce_prefix, size_t ep_len,
                                    const uint8_t *extranonce_2, size_t e2_len,
                                    const uint8_t *suffix, size_t suffix_len,
                                    uint8_t dest[32]);

void calculate_merkle_root_hash(const uint8_t coinbase_tx_hash[32], const uint8_t merkle_branches[][32], const int num_merkle_branches, uint8_t dest[32]);

/* Converts a little-endian 256-bit value to pool difficulty. */
double hash_to_pdiff(const uint8_t hash[32]);

double mining_nonce_difficulty(const asic_job_t *job, const uint32_t nonce, const uint32_t rolled_version);

uint32_t increment_bitmask(const uint32_t value, const uint32_t mask);

#endif /* MINING_H_ */
