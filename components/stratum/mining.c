#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include "psa/crypto.h"
#include "mining.h"
#include "utils.h"

bool calculate_coinbase_tx_hash_bin(const uint8_t *prefix, size_t prefix_len,
                                    const uint8_t *extranonce_prefix, size_t ep_len,
                                    const uint8_t *extranonce_2, size_t e2_len,
                                    const uint8_t *suffix, size_t suffix_len,
                                    uint8_t dest[32])
{
    if (!dest) {
        return false;
    }

    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    psa_hash_setup(&op, PSA_ALG_SHA_256);
    if (prefix && prefix_len > 0) {
        psa_hash_update(&op, prefix, prefix_len);
    }
    if (extranonce_prefix && ep_len > 0) {
        psa_hash_update(&op, extranonce_prefix, ep_len);
    }
    if (extranonce_2 && e2_len > 0) {
        psa_hash_update(&op, extranonce_2, e2_len);
    }
    if (suffix && suffix_len > 0) {
        psa_hash_update(&op, suffix, suffix_len);
    }
    uint8_t first_hash[32];
    size_t out_len = 0;
    if (psa_hash_finish(&op, first_hash, sizeof(first_hash), &out_len) != PSA_SUCCESS ||
        out_len != sizeof(first_hash)) {
        psa_hash_abort(&op);
        memset(dest, 0, 32);
        return false;
    }

    if (psa_hash_compute(PSA_ALG_SHA_256, first_hash, sizeof(first_hash),
                         dest, 32, &out_len) != PSA_SUCCESS || out_len != 32) {
        memset(dest, 0, 32);
        return false;
    }

    return true;
}

void calculate_merkle_root_hash(const uint8_t coinbase_tx_hash[32], const uint8_t merkle_branches[][32], const int num_merkle_branches, uint8_t dest[32])
{
    uint8_t both_merkles[64];
    memcpy(both_merkles, coinbase_tx_hash, 32);
    for (int i = 0; i < num_merkle_branches; i++) {
        memcpy(both_merkles + 32, merkle_branches[i], 32);
        double_sha256_bin(both_merkles, 64, both_merkles);
    }

    memcpy(dest, both_merkles, 32);
}

bool mining_nonce_hash(const asic_job_t *job, const uint32_t nonce, const uint32_t rolled_version, uint8_t hash[32])
{
    uint8_t header[80];
    asic_job_header(job, nonce, rolled_version, header);
    return double_sha256_bin(header, sizeof(header), hash);
}

uint32_t increment_bitmask(const uint32_t value, const uint32_t mask)
{
    // Carry across gaps in the mask while preserving every unmasked bit.
    return (value & ~mask) | (((value | ~mask) + 1U) & mask);
}
