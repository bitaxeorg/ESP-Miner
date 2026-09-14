#include "bm_job_reference.h"
#include "bm_job_midstate.h"
#include "mining.h"
#include "utils.h"
#include <string.h>

void build_reference_bm_job(const miner_job_t *job, const uint32_t version, const uint8_t merkle_root[32], const uint32_t version_mask, const double difficulty, const uint8_t software_midstates, bm_job *destination)
{
    destination->version = (version != 0) ? version : job->version;
    destination->nbits = job->nbits;
    destination->ntime = job->ntime;
    destination->starting_nonce = 0;
    destination->pool_diff = (job->pool_diff > 0) ? job->pool_diff : difficulty;
    destination->pool_id = job->pool_id;
    destination->job_type = job->type;
    uint32_t effective_mask = (job->version_mask != 0) ? job->version_mask : version_mask;
    destination->version_mask = effective_mask;
    destination->num_midstates = 0;
    reverse_32bit_words(merkle_root, destination->merkle_root);
    reverse_32bit_words(job->prev_hash, destination->prev_block_hash);

    if (software_midstates == 0)
    {
        return;
    }

    uint8_t midstate_data[64];
    memcpy(midstate_data + 4, job->prev_hash, 32);
    memcpy(midstate_data + 36, merkle_root, 28);

    uint32_t current_version = destination->version;
    uint8_t midstate[32];

    for (int i = 0; i < software_midstates && i < BM_JOB_MAX_MIDSTATES; i++)
    {
        if (i > 0)
        {
            if (effective_mask == 0)
            {
                break;
            }
            current_version = increment_bitmask(current_version, effective_mask);
        }
        memcpy(midstate_data, &current_version, 4);
        midstate_sha256_bin(midstate_data, 64, midstate);
        reverse_32bit_words(midstate, destination->midstates[i]);
        destination->num_midstates++;
    }
}
