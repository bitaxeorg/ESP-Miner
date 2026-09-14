/* Frozen constructor from PR #1969 (9af07d7c), used only as a compatibility
 * oracle. Production job creation must go through the common-job adapter. */
#include "legacy_bm_job.h"
#include "bm_job_midstate.h"
#include "mining.h"
#include "utils.h"
#include <string.h>

void legacy_construct_bm_job(const miner_job_t *job, const uint32_t version, const uint8_t merkle_root[32], const uint32_t version_mask, const double difficulty, const uint8_t software_midstates, bm_job *new_job)
{
    new_job->version = (version != 0) ? version : job->version;
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
