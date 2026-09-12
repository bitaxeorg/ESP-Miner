#ifndef BM_JOB_ADAPTER_H_
#define BM_JOB_ADAPTER_H_

#include "asic_job.h"
#include "mining.h"

/* Convert common work at the Bitmain boundary. On success the destination
 * owns its metadata and can be transferred to the existing send functions.
 * On failure it is unchanged. Release successful jobs with free_bm_job. */
bool bm_job_build_from_asic_job(const asic_job_t *source,
                               uint8_t software_midstates, bm_job *destination);

#endif
