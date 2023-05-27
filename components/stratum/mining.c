#include <string.h>
#include <stdio.h>
#include <limits.h>
#include "mining.h"
#include "utils.h"
#include "../../main/pretty.h"
#include "mbedtls/sha256.h"

void free_bm_job(bm_job * job)
{
    free(job->jobid);
    free(job->extranonce2);
    free(job);
}

char * construct_coinbase_tx(const char * coinbase_1, const char * coinbase_2,
                             const char * extranonce, const char * extranonce_2)
{
    int coinbase_tx_len = strlen(coinbase_1) + strlen(coinbase_2)
        + strlen(extranonce) + strlen(extranonce_2) + 1;

    char * coinbase_tx = malloc(coinbase_tx_len);
    strcpy(coinbase_tx, coinbase_1);
    strcat(coinbase_tx, extranonce);
    strcat(coinbase_tx, extranonce_2);
    strcat(coinbase_tx, coinbase_2);
    coinbase_tx[coinbase_tx_len - 1] = '\0';

    return coinbase_tx;
}

char * calculate_merkle_root_hash(const char * coinbase_tx, const uint8_t merkle_branches[][32], const int num_merkle_branches)
{
    size_t coinbase_tx_bin_len = strlen(coinbase_tx) / 2;
    uint8_t * coinbase_tx_bin = malloc(coinbase_tx_bin_len);
    hex2bin(coinbase_tx, coinbase_tx_bin, coinbase_tx_bin_len);

    uint8_t both_merkles[64];
    uint8_t * new_root = double_sha256_bin(coinbase_tx_bin, coinbase_tx_bin_len);
    free(coinbase_tx_bin);
    memcpy(both_merkles, new_root, 32);
    free(new_root);
    for (int i = 0; i < num_merkle_branches; i++)
    {
        memcpy(both_merkles + 32, merkle_branches[i], 32);
        uint8_t * new_root = double_sha256_bin(both_merkles, 64);
        memcpy(both_merkles, new_root, 32);
        free(new_root);
    }

    char * merkle_root_hash = malloc(65);
    bin2hex(both_merkles, 32, merkle_root_hash, 65);
    return merkle_root_hash;
}

//take a mining_notify struct with ascii hex strings and convert it to a bm_job struct
bm_job construct_bm_job(mining_notify * params, const char * merkle_root) {
    bm_job new_job;

    new_job.version = params->version;
    new_job.starting_nonce = 0;
    new_job.target = params->target;
    new_job.ntime = params->ntime;

    hex2bin(merkle_root, new_job.merkle_root, 32);
    hex2bin(params->prev_block_hash, new_job.prev_block_hash, 32);

    ////make the midstate hash
    uint8_t midstate_data[64];

    //print the header
    printf("header: %08x%s%s%08x%08x00000000\n", params->version, params->prev_block_hash, merkle_root, params->ntime, params->target);

    //copy 68 bytes header data into midstate (and deal with endianess)
    memcpy(midstate_data, &new_job.version, 4); //copy version
    swap_endian_words(params->prev_block_hash, midstate_data + 4); //copy prev_block_hash
    memcpy(midstate_data + 36, new_job.merkle_root, 28); //copy merkle_root

    printf("midstate_data: ");
    prettyHex(midstate_data, 64);
    printf("\n");

    midstate_sha256_bin(midstate_data, 64, new_job.midstate); //make the midstate hash
    reverse_bytes(new_job.midstate, 32); //reverse the midstate bytes for the BM job packet

    return new_job;
}

char * extranonce_2_generate(uint32_t extranonce_2, uint32_t length)
{
    char * extranonce_2_str = malloc(length * 2 + 1);
    bin2hex((uint8_t *) &extranonce_2, length, extranonce_2_str, length * 2 + 1);
    return extranonce_2_str;
}

///////cgminer nonce testing
/* truediffone == 0x00000000FFFF0000000000000000000000000000000000000000000000000000
*/
static const double truediffone = 26959535291011309493156476344723991336010898738574164086137773096960.0;

/* testing a nonce and return the diff - 0 means invalid */
double test_nonce_value(bm_job * job, uint32_t nonce) {
	double d64, s64, ds;
    unsigned char header[80];

    //copy data from job to header
    memcpy(header, &job->version, 4);
    memcpy(header + 4, job->prev_block_hash, 32);
    memcpy(header + 36, job->merkle_root, 32);
    memcpy(header + 68, &job->ntime, 4);
    memcpy(header + 72, &job->target, 4);
    memcpy(header + 76, &nonce, 4);

	unsigned char swapped_header[80];
	unsigned char hash_buffer[32];
    unsigned char hash_result[32];

    printf("data32: ");
    prettyHex(header, 80);

    //endian flip the first 80 bytes.
    //version (4 bytes), prevhash (32 bytes), merkle (32 bytes), time (4 bytes), bits (4 bytes), nonce (4 bytes) = 80 bytes
	flip80bytes((uint32_t *)swapped_header, header);

    //double hash the header
	mbedtls_sha256(swapped_header, 80, hash_buffer, 0);
	mbedtls_sha256(hash_buffer, 32, hash_result, 0);

    printf("hash: ");
    prettyHex(hash_result, 32);
    // //check that the last 4 bytes are 0
	// if (*hash_32 != 0) {
	// 	return 0.0;
    // }

	d64 = truediffone;
	s64 = le256todouble(hash_result);
	ds = d64 / s64;

	return ds;
}