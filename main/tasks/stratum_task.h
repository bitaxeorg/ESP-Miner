#ifndef STRATUM_TASK_H_
#define STRATUM_TASK_H_

#include "asic_job.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct GlobalState GlobalState;

// Manages network connectivity, pool failover/heartbeat, and dispatches to V1/V2 protocol drivers.
void stratum_task(void *pvParameters);

// Returns true if a reconnect request is pending.
bool stratum_reconnect_requested(void);

// Notify stratum task that a specific pool configuration was modified in NVS.
void stratum_notify_pool_modified(GlobalState *gs, uint16_t pool_idx);

// Notify stratum task that primary/secondary pool index or fallback toggle was changed.
void stratum_notify_pool_selection_changed(GlobalState *gs);

// Abort active stratum connection immediately for the given pool.
void stratum_abort_pool(uint16_t pool_idx);

// Abort active stratum connection immediately across all pools.
void stratum_abort_connection(void);

// Submit a found share to the active pool (dispatches to SV1 or SV2).
int stratum_submit_share(GlobalState *GLOBAL_STATE, const asic_job_t *active_job,
                         uint32_t nonce, uint32_t rolled_version, uint64_t *sent_time_us);

// Set connection error for the given pool without overwriting primary status while on fallback.
void stratum_set_pool_error(GlobalState *gs, uint16_t pool_idx, const char *err_msg);

// Check if primary has received work during fallback and trigger handover by aborting fallback.
void stratum_check_primary_handover(GlobalState *gs, uint16_t pool_idx);

#endif /* STRATUM_TASK_H_ */
