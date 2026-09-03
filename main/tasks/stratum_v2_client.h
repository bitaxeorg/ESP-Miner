#ifndef STRATUM_V2_CLIENT_H_
#define STRATUM_V2_CLIENT_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "mining.h"

typedef struct GlobalState GlobalState;

// Run the Stratum V2 client loop for pool_idx until disconnect, ASIC pause, or reconnect requested.
esp_err_t stratum_v2_run(GlobalState *GLOBAL_STATE, uint16_t pool_idx);

void stratum_v2_close_connection(GlobalState *GLOBAL_STATE);
int stratum_v2_submit_share(GlobalState *GLOBAL_STATE, const bm_job *active_job,
                            uint32_t nonce, uint32_t rolled_version, uint64_t *sent_time_us);

// Probe a Stratum V2 pool to check reachability, Noise handshake, and credentials
bool stratum_v2_probe_pool(GlobalState *GLOBAL_STATE, uint16_t pool_idx);

#endif // STRATUM_V2_CLIENT_H_
