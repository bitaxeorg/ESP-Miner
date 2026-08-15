#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include "psa/crypto.h"
#include "mining.h"
#include "stratum_api.h"
#include "utils.h"

void free_bm_job(bm_job *job)
{
    free(job->jobid);
    free(job->extranonce2);
    free(job);
}

#define MAX_EXTRANONCE_2_BYTES 32
#define MAX_COINBASE_TX_BYTES (STRATUM_V1_MAX_JSON_LINE_SIZE / 2)
#define HEX_HASH_CHUNK_BYTES 128

static int hex_nibble(unsigned char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

static bool hash_hex_string(psa_hash_operation_t *operation, const char *hex,
                            size_t *total_bytes)
{
    if (hex == NULL) {
        return false;
    }

    uint8_t decoded[HEX_HASH_CHUNK_BYTES];
    size_t decoded_len = 0;

    while (*hex != '\0') {
        if (hex[1] == '\0') {
            return false;
        }

        int high = hex_nibble((unsigned char)hex[0]);
        int low = hex_nibble((unsigned char)hex[1]);
        if (high < 0 || low < 0 ||
            *total_bytes >= MAX_COINBASE_TX_BYTES) {
            return false;
        }

        decoded[decoded_len++] = (uint8_t)((high << 4) | low);
        (*total_bytes)++;
        hex += 2;

        if (decoded_len == sizeof(decoded)) {
            if (psa_hash_update(operation, decoded, decoded_len) != PSA_SUCCESS) {
                return false;
            }
            decoded_len = 0;
        }
    }

    return decoded_len == 0 ||
           psa_hash_update(operation, decoded, decoded_len) == PSA_SUCCESS;
}

bool calculate_coinbase_tx_hash(const char *coinbase_1, const char *coinbase_2,
                                const char *extranonce, const char *extranonce_2,
                                uint8_t dest[32])
{
    if (dest == NULL) {
        return false;
    }

    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    if (psa_hash_setup(&operation, PSA_ALG_SHA_256) != PSA_SUCCESS) {
        return false;
    }

    size_t total_bytes = 0;
    uint8_t first_hash[32];
    size_t first_hash_len = 0;
    bool valid = hash_hex_string(&operation, coinbase_1, &total_bytes) &&
                 hash_hex_string(&operation, extranonce, &total_bytes) &&
                 hash_hex_string(&operation, extranonce_2, &total_bytes) &&
                 hash_hex_string(&operation, coinbase_2, &total_bytes) &&
                 total_bytes > 0 &&
                 psa_hash_finish(&operation, first_hash, sizeof(first_hash),
                                 &first_hash_len) == PSA_SUCCESS &&
                 first_hash_len == sizeof(first_hash);

    if (!valid) {
        psa_hash_abort(&operation);
        return false;
    }

    size_t output_len = 0;
    return psa_hash_compute(PSA_ALG_SHA_256, first_hash, sizeof(first_hash),
                            dest, 32, &output_len) == PSA_SUCCESS &&
           output_len == 32;
}

void calculate_coinbase_tx_hash_bin(const uint8_t *prefix, size_t prefix_len,
                                    const uint8_t *extranonce_prefix, size_t ep_len,
                                    const uint8_t *extranonce_2, size_t e2_len,
                                    const uint8_t *suffix, size_t suffix_len,
                                    uint8_t dest[32])
{
    size_t total_len = prefix_len + ep_len + e2_len + suffix_len;
    uint8_t *buf = malloc(total_len);
    if (!buf) return;

    size_t offset = 0;
    memcpy(buf + offset, prefix, prefix_len);   offset += prefix_len;
    memcpy(buf + offset, extranonce_prefix, ep_len); offset += ep_len;
    memcpy(buf + offset, extranonce_2, e2_len); offset += e2_len;
    memcpy(buf + offset, suffix, suffix_len);

    double_sha256_bin(buf, total_len, dest);
    free(buf);
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

// take a mining_notify struct with ascii hex strings and convert it to a bm_job struct
void construct_bm_job(mining_notify *params, const uint8_t merkle_root[32], const uint32_t version_mask, const double difficulty, bm_job *new_job)
{
    new_job->version = params->version;
    new_job->target = params->target;
    new_job->ntime = params->ntime;
    new_job->starting_nonce = 0;
    new_job->pool_diff = difficulty;
    reverse_32bit_words(merkle_root, new_job->merkle_root);

    uint8_t prev_block_hash[32];
    hex2bin(params->prev_block_hash, prev_block_hash, 32);
    reverse_endianness_per_word(prev_block_hash);
    reverse_32bit_words(prev_block_hash, new_job->prev_block_hash);

    // make the midstate hash
    uint8_t midstate_data[64];

    // copy 64 bytes header data into midstate (and deal with endianess)
    memcpy(midstate_data, &new_job->version, 4);      // copy version
    memcpy(midstate_data + 4, prev_block_hash, 32);   // copy prev_block_hash
    memcpy(midstate_data + 36, merkle_root, 28);      // copy merkle_root

    uint8_t midstate[32];
    midstate_sha256_bin(midstate_data, 64, midstate); // make the midstate hash
    reverse_32bit_words(midstate, new_job->midstate); // reverse the midstate words for the BM job packet

    if (version_mask != 0)
    {
        uint32_t rolled_version = increment_bitmask(new_job->version, version_mask);
        memcpy(midstate_data, &rolled_version, 4);
        midstate_sha256_bin(midstate_data, 64, midstate);
        reverse_32bit_words(midstate, new_job->midstate1);

        rolled_version = increment_bitmask(rolled_version, version_mask);
        memcpy(midstate_data, &rolled_version, 4);
        midstate_sha256_bin(midstate_data, 64, midstate);
        reverse_32bit_words(midstate, new_job->midstate2);

        rolled_version = increment_bitmask(rolled_version, version_mask);
        memcpy(midstate_data, &rolled_version, 4);
        midstate_sha256_bin(midstate_data, 64, midstate);
        reverse_32bit_words(midstate, new_job->midstate3);
        new_job->num_midstates = 4;
    }
    else
    {
        new_job->num_midstates = 1;
    }
}

bool extranonce_2_generate(uint64_t extranonce_2, uint32_t length,
                           char *dest, size_t dest_len)
{
    if (dest == NULL || length > MAX_EXTRANONCE_2_BYTES ||
        dest_len < (size_t)length * 2U + 1U) {
        return false;
    }

    uint8_t extranonce_2_bytes[MAX_EXTRANONCE_2_BYTES];
    memset(extranonce_2_bytes, 0, length);
    
    // Copy the extranonce_2 value into the buffer, handling endianness
    // Copy up to the size of uint64_t or the requested length, whichever is smaller
    size_t copy_len = (length < sizeof(uint64_t)) ? length : sizeof(uint64_t);
    memcpy(extranonce_2_bytes, &extranonce_2, copy_len);
    
    // Convert the bytes to hex string
    return bin2hex(extranonce_2_bytes, length, dest, dest_len) ==
           (size_t)length * 2U;
}

double hash_to_pdiff(const uint8_t hash[32])
{
    double s64 = le256todouble(hash);
    if (s64 == 0.0) return (double)UINT32_MAX;
    return truediffone / s64;
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
