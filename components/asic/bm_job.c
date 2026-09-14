#include "bm_job.h"
#include "bm_job_midstate.h"
#include "mining.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>

bool bm_job_build_from_asic_job(const asic_job_t *source,
                               uint8_t software_midstates, bm_job *destination)
{
    if (source == NULL || destination == NULL ||
        memchr(source->job_id, 0, sizeof(source->job_id)) == NULL ||
        memchr(source->extranonce2, 0, sizeof(source->extranonce2)) == NULL) {
        return false;
    }
    bm_job job = {
        .version = source->version,
        .version_mask = source->version_mask,
        .nbits = source->nbits,
        .ntime = source->ntime,
        .starting_nonce = source->starting_nonce,
        .pool_diff = source->pool_diff,
        .pool_id = source->pool_id,
        .job_type = source->source_type,
    };
    memcpy(job.job_id, source->job_id, strlen(source->job_id) + 1);
    memcpy(job.extranonce2, source->extranonce2, strlen(source->extranonce2) + 1);
    reverse_32bit_words(source->prev_hash, job.prev_block_hash);
    reverse_32bit_words(source->merkle_root, job.merkle_root);
    uint8_t header[80];
    uint8_t midstate[32];
    uint32_t version = source->version;
    for (uint8_t i = 0; i < software_midstates && i < BM_JOB_MAX_MIDSTATES; ++i) {
        if (i > 0) {
            if (source->version_mask == 0) {
                break;
            }
            version = increment_bitmask(version, source->version_mask);
        }
        asic_job_header(source, source->starting_nonce, version, header);
        midstate_sha256_bin(header, 64, midstate);
        reverse_32bit_words(midstate, job.midstates[i]);
        ++job.num_midstates;
    }
    *destination = job;
    return true;
}

void free_bm_job(bm_job *job)
{
    free(job);
}

bool bm_job_to_asic_job(const bm_job *source, asic_job_t *destination)
{
    if (source == NULL || destination == NULL) {
        return false;
    }
    size_t job_id_length = strnlen(source->job_id, ASIC_JOB_ID_LEN);
    size_t extranonce2_length = strnlen(source->extranonce2, ASIC_JOB_EXTRANONCE2_HEX_SIZE);
    if (job_id_length == ASIC_JOB_ID_LEN || extranonce2_length == ASIC_JOB_EXTRANONCE2_HEX_SIZE) {
        return false;
    }
    asic_job_t job = {
        .version = source->version,
        .version_mask = source->version_mask,
        .ntime = source->ntime,
        .nbits = source->nbits,
        .starting_nonce = source->starting_nonce,
        .pool_diff = source->pool_diff,
        .pool_id = source->pool_id,
        .source_type = source->job_type,
    };
    reverse_32bit_words(source->prev_block_hash, job.prev_hash);
    reverse_32bit_words(source->merkle_root, job.merkle_root);
    memcpy(job.job_id, source->job_id, job_id_length + 1);
    memcpy(job.extranonce2, source->extranonce2, extranonce2_length + 1);
    *destination = job;
    return true;
}
