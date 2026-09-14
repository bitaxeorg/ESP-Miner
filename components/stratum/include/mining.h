#ifndef MINING_H_
#define MINING_H_

#include <stdint.h>
#include <stddef.h>
#include "miner_job.h"

/* Build complete owned work from an accepted pool job. The destination is
 * unchanged on failure. Version zero keeps the existing source fallback. */
bool mining_build_asic_job(const miner_job_t *source, uint64_t extranonce2,
                           uint32_t version, asic_job_t *destination);

void calculate_coinbase_tx_hash_bin(const uint8_t *prefix, size_t prefix_len,
                                    const uint8_t *extranonce_prefix, size_t ep_len,
                                    const uint8_t *extranonce_2, size_t e2_len,
                                    const uint8_t *suffix, size_t suffix_len,
                                    uint8_t dest[32]);

void calculate_merkle_root_hash(const uint8_t coinbase_tx_hash[32], const uint8_t merkle_branches[][32], const int num_merkle_branches, uint8_t dest[32]);

// Convert a 256-bit value (block hash or pool target, little-endian) to
// difficulty (pdiff = truediffone / value). Used by nonce validation and SV2
// target messages. Returns a double to preserve fractional difficulty.
double hash_to_pdiff(const uint8_t hash[32]);

/* Validate a nonce using common Bitcoin header bytes, independent of hardware. */
double test_nonce_value(const asic_job_t *job, const uint32_t nonce, const uint32_t rolled_version);

uint32_t increment_bitmask(const uint32_t value, const uint32_t mask);

#endif /* MINING_H_ */
