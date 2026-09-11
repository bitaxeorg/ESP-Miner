#ifndef TEST_FIXTURE_SYSTEM_H
#define TEST_FIXTURE_SYSTEM_H

#include "global_state.h"
#include "miner_job.h"

void SYSTEM_decode_and_apply_coinbase(GlobalState *state,
                                      const miner_job_t *job);

#endif /* TEST_FIXTURE_SYSTEM_H */
