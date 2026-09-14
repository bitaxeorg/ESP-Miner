#ifndef BM_JOB_H_
#define BM_JOB_H_

#include "asic_job.h"
#include <stdbool.h>

#define BM_JOB_MAX_MIDSTATES 4

typedef struct bm_job
{
    uint32_t version;
    uint32_t version_mask;
    uint8_t prev_block_hash[32];
    uint8_t merkle_root[32];
    uint32_t ntime;
    uint32_t nbits;
    uint32_t starting_nonce;

    uint8_t num_midstates;
    uint8_t midstates[BM_JOB_MAX_MIDSTATES][32];
    double pool_diff;
    uint8_t pool_id;
    mining_job_source_t job_type;
    char *job_id;
    char *extranonce2;
} bm_job;

void free_bm_job(bm_job *job);

typedef struct GlobalState GlobalState;
/* Takes ownership of the job. */
void ASIC_send_work(GlobalState *state, bm_job *job);

/* Destination is unchanged if metadata is missing or too long. */
bool bm_job_to_asic_job(const bm_job *source, asic_job_t *destination);

/* Allocates destination metadata on success; leaves destination unchanged
 * on failure. The caller owns the allocated strings. */
bool bm_job_build_from_asic_job(const asic_job_t *source,
                               uint8_t software_midstates, bm_job *destination);

#endif
