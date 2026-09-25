#ifndef TEST_STUB_SYSTEM_H
#define TEST_STUB_SYSTEM_H

#include "global_state.h"
#include "miner_job.h"

void SYSTEM_decode_and_apply_coinbase(GlobalState *state,
                                      const miner_job_t *job);
void SYSTEM_notify_found_nonce(GlobalState *state, double difficulty,
                               uint32_t target);

void SYSTEM_noinit_update(SystemModule *system);

#endif /* TEST_STUB_SYSTEM_H */
