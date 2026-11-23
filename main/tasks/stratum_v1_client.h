#ifndef STRATUM_V1_CLIENT_H_
#define STRATUM_V1_CLIENT_H_

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include "asic_job.h"

typedef struct GlobalState GlobalState;

// Run the Stratum V1 client loop for pool_idx until disconnect, ASIC pause, or reconnect requested.
esp_err_t stratum_v1_run(GlobalState *GLOBAL_STATE, uint16_t pool_idx);

void stratum_v1_close_connection(GlobalState *GLOBAL_STATE, uint16_t pool_idx);

// Abort active Stratum V1 socket connection immediately
void stratum_v1_abort_connection(uint16_t pool_idx);

// Submit a solved ASIC share to the active Stratum V1 pool connection
int stratum_v1_submit_share(GlobalState *GLOBAL_STATE, const asic_job_t *active_job, uint32_t nonce, uint32_t rolled_version, uint64_t *sent_time_us);

struct stratum_driver;
extern const struct stratum_driver g_stratum_v1_driver;

#endif // STRATUM_V1_CLIENT_H_
