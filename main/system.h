#ifndef SYSTEM_H_
#define SYSTEM_H_

#include "esp_err.h"
#include "global_state.h"
#include "sv2_protocol.h"

void SYSTEM_init_system(GlobalState * GLOBAL_STATE);
void SYSTEM_init_versions(GlobalState * GLOBAL_STATE);
esp_err_t SYSTEM_init_peripherals(GlobalState * GLOBAL_STATE);

// Clear the stratum job queue and valid-job tracking on a clean-jobs event,
// and reset hashrate measurements so reconnects don't spike the average.
// Shared by the SV1 and SV2 tasks.
void SYSTEM_clean_jobs_queue(GlobalState * GLOBAL_STATE);

void SYSTEM_notify_accepted_share(GlobalState * GLOBAL_STATE);
void SYSTEM_notify_rejected_share(GlobalState * GLOBAL_STATE, char * error_msg);
void SYSTEM_notify_found_nonce(GlobalState * GLOBAL_STATE, double diff, uint32_t target);
void SYSTEM_notify_new_ntime(GlobalState * GLOBAL_STATE, uint32_t ntime);

stratum_protocol_t stratum_protocol_from_string(const char *s);
sv2_channel_type_t sv2_channel_type_from_string(const char *s);
void SYSTEM_load_pool_from_nvs(GlobalState * GLOBAL_STATE, int i);

#endif /* SYSTEM_H_ */
