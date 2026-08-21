#include <string.h>
#include <stdio.h>
#include <limits.h>
#include "esp_log.h"
#include "mining.h"
#include "stratum_api.h"
#include "utils.h"

static const char *TAG = "mining";

void free_bm_job(bm_job *job)
{
    free(job->jobid);
    free(job->extranonce2);
    free(job);
}


void calculate_coinbase_tx_hash_bin(const uint8_t *prefix, size_t prefix_len,
                                    const uint8_t *extranonce_prefix, size_t ep_len,
                                    const uint8_t *extranonce_2, size_t e2_len,
                                    const uint8_t *suffix, size_t suffix_len,
                                    uint8_t dest[32])
{
    size_t total_len = prefix_len + ep_len + e2_len + suffix_len;
    uint8_t stack_buf[1024];
    uint8_t *buf = (total_len <= sizeof(stack_buf)) ? stack_buf : malloc(total_len);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate memory for coinbase tx (%zu bytes)", total_len);
        if (dest) memset(dest, 0, 32);
        return;
    }

    size_t offset = 0;
    if (prefix && prefix_len > 0) {
        memcpy(buf + offset, prefix, prefix_len);
        offset += prefix_len;
    }
    if (extranonce_prefix && ep_len > 0) {
        memcpy(buf + offset, extranonce_prefix, ep_len);
        offset += ep_len;
    }
    if (extranonce_2 && e2_len > 0) {
        memcpy(buf + offset, extranonce_2, e2_len);
        offset += e2_len;
    }
    if (suffix && suffix_len > 0) {
        memcpy(buf + offset, suffix, suffix_len);
        offset += suffix_len;
    }

    double_sha256_bin(buf, total_len, dest);
    if (buf != stack_buf) {
        free(buf);
    }
}

void construct_bm_job_from_miner_job(const miner_job_t *job, const uint8_t merkle_root[32], const uint32_t version_mask, const double difficulty, const uint8_t software_midstates, bm_job *new_job)
{
    new_job->version = job->version;
    new_job->target = job->nbits;
    new_job->ntime = job->ntime;
    new_job->starting_nonce = 0;
    new_job->pool_diff = (job->pool_diff > 0) ? job->pool_diff : difficulty;
    new_job->pool_id = job->pool_id;
    new_job->job_type = job->type;
    uint32_t effective_mask = (job->version_mask != 0) ? job->version_mask : version_mask;
    new_job->version_mask = effective_mask;
    new_job->num_midstates = 0;
    reverse_32bit_words(merkle_root, new_job->merkle_root);
    reverse_32bit_words(job->prev_hash, new_job->prev_block_hash);

    if (software_midstates == 0)
    {
        return;
    }

    // make the midstate hash
    uint8_t midstate_data[64];
    memcpy(midstate_data + 4, job->prev_hash, 32);
    memcpy(midstate_data + 36, merkle_root, 28);

    uint32_t current_ver = new_job->version;
    uint8_t midstate[32];

    for (int i = 0; i < software_midstates && i < BM_JOB_MAX_MIDSTATES; i++)
    {
        if (i > 0)
        {
            if (effective_mask == 0)
            {
                break;
            }
            current_ver = increment_bitmask(current_ver, effective_mask);
        }
        memcpy(midstate_data, &current_ver, 4);
        midstate_sha256_bin(midstate_data, 64, midstate);
        reverse_32bit_words(midstate, new_job->midstates[i]);
        new_job->num_midstates++;
    }
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


#include <math.h>

double hash_to_pdiff(const uint8_t hash[32])
{
    if (!hash) return (double)UINT32_MAX;
    double s64 = le256todouble(hash);
    if (s64 <= 0.0 || isnan(s64) || isinf(s64)) return (double)UINT32_MAX;
    double diff = truediffone / s64;
    if (isnan(diff) || isinf(diff) || diff <= 0.0) return (double)UINT32_MAX;
    return diff;
}

///////cgminer nonce testing
/* testing a nonce and return the diff - 0 means invalid */
double test_nonce_value(const bm_job *job, const uint32_t nonce, const uint32_t rolled_version)
{
    uint8_t header[80];

    // // TODO: use the midstate hash instead of hashing the whole header
    // uint32_t rolled_version = job->version;
    // for (int i = 0; i < midstate_index; i++) {
    //     rolled_version = increment_bitmask(rolled_version, job->version_mask);
    // }

    // copy data from job to header
    memcpy(header, &rolled_version, 4);
    reverse_32bit_words(job->prev_block_hash, header + 4);
    reverse_32bit_words(job->merkle_root, header + 36);
    memcpy(header + 68, &job->ntime, 4);
    memcpy(header + 72, &job->target, 4);
    memcpy(header + 76, &nonce, 4);

    uint8_t hash_result[32];
    double_sha256_bin(header, 80, hash_result);

    return hash_to_pdiff(hash_result);
}

uint32_t increment_bitmask(const uint32_t value, const uint32_t mask)
{
    // if mask is zero, just return the original value
    if (mask == 0)
        return value;

    uint32_t carry = (value & mask) + (mask & -mask);      // increment the least significant bit of the mask
    uint32_t overflow = carry & ~mask;                     // find overflowed bits that are not in the mask
    uint32_t new_value = (value & ~mask) | (carry & mask); // set bits according to the mask

    // Handle carry propagation
    if (overflow > 0)
    {
        uint32_t carry_mask = (overflow << 1);                // shift left to get the mask where carry should be propagated
        new_value = increment_bitmask(new_value, carry_mask); // recursively handle carry propagation
    }

    return new_value;
}
