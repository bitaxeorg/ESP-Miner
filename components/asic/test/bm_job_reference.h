#ifndef BM_JOB_REFERENCE_H_
#define BM_JOB_REFERENCE_H_

#include "bm_job.h"
#include "miner_job.h"

void build_reference_bm_job(const miner_job_t *job, const uint32_t version, const uint8_t merkle_root[32], const uint32_t version_mask, const double difficulty, const uint8_t software_midstates, bm_job *destination);

#endif
