#include "bm_job.h"
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
        .target = source->nbits,
        .ntime = source->ntime,
        .starting_nonce = source->starting_nonce,
        .pool_diff = source->pool_diff,
        .pool_id = source->pool_id,
        .job_type = source->source_type,
    };
    job.jobid = strdup(source->job_id);
    job.extranonce2 = strdup(source->extranonce2);
    if (job.jobid == NULL || job.extranonce2 == NULL) {
        free(job.jobid);
        free(job.extranonce2);
        return false;
    }
    reverse_32bit_words(source->prev_hash, job.prev_block_hash);
    reverse_32bit_words(source->merkle_root, job.merkle_root);
    uint8_t header[80];
    uint8_t midstate[32];
    uint32_t version = source->version;
    for (uint8_t i = 0; i < software_midstates && i < BM_JOB_MAX_MIDSTATES; ++i) {
        if (i > 0) {
            if (source->version_mask == 0) break;
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
