#ifndef STRATUM_V2_CLIENT_H_
#define STRATUM_V2_CLIENT_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "asic_job.h"

typedef struct GlobalState GlobalState;

// Run the Stratum V2 client loop for pool_idx until disconnect, ASIC pause, or reconnect requested.
esp_err_t stratum_v2_run(GlobalState *GLOBAL_STATE, uint16_t pool_idx);

void stratum_v2_close_connection(GlobalState *GLOBAL_STATE);
void stratum_v2_abort_connection(void);
int stratum_v2_submit_share(GlobalState *GLOBAL_STATE, const asic_job_t *active_job,
                            uint32_t nonce, uint32_t rolled_version, uint64_t *sent_time_us);

// Probe a Stratum V2 pool to check reachability, Noise handshake, and credentials
bool stratum_v2_probe_pool(GlobalState *GLOBAL_STATE, uint16_t pool_idx);

struct stratum_driver;
extern const struct stratum_driver g_stratum_v2_driver;

#endif // STRATUM_V2_CLIENT_H_
