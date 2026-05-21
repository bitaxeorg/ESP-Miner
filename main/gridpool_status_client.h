#ifndef GRIDPOOL_STATUS_CLIENT_H_
#define GRIDPOOL_STATUS_CLIENT_H_

#include "esp_err.h"
#include "global_state.h"
#include "mining.h"

esp_err_t gridpool_status_client_start(GlobalState *GLOBAL_STATE);
bool gridpool_direct_is_enabled(void);
esp_err_t gridpool_direct_submit_share(GlobalState *GLOBAL_STATE,
                                       const bm_job *job,
                                       uint32_t nonce,
                                       uint32_t rolled_version,
                                       double difficulty,
                                       int64_t found_timestamp_us);
esp_err_t gridpool_direct_submit_block(GlobalState *GLOBAL_STATE,
                                       const bm_job *job,
                                       uint32_t nonce,
                                       uint32_t rolled_version);

#endif // GRIDPOOL_STATUS_CLIENT_H_
