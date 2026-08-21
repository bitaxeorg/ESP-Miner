#ifndef STRATUM_TASK_H_
#define STRATUM_TASK_H_

#include "global_state.h"
#include <stdbool.h>

// Unified persistent Stratum Task entry point.
// Manages network connectivity, pool failover/heartbeat, and dispatches to V1/V2 protocol drivers.
void stratum_task(void *pvParameters);

// Request the active stratum client to reconnect (e.g. on primary pool recovery or pool settings change).
void stratum_request_reconnect(void);

// Probe a pool to check if it is reachable without disrupting active mining.
bool stratum_probe_pool(GlobalState *gs, uint16_t pool_idx);

// Promptly trigger a heartbeat check of the primary pool (e.g. when primary config changes while on fallback).
void stratum_trigger_heartbeat_check(void);

// Notify stratum task that a specific pool configuration was modified in NVS.
void stratum_notify_pool_modified(GlobalState *gs, uint16_t pool_idx);

// Notify stratum task that primary/secondary pool index or fallback toggle was changed.
void stratum_notify_pool_selection_changed(GlobalState *gs);

// Submit a found share to the active pool (dispatches to SV1 or SV2).
int stratum_submit_share(GlobalState *GLOBAL_STATE, const bm_job *active_job,
                         uint32_t nonce, uint32_t rolled_version, uint64_t *sent_time_us);

#endif /* STRATUM_TASK_H_ */
