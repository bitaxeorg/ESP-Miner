#ifndef LOCAL_WORK_CLIENT_H_
#define LOCAL_WORK_CLIENT_H_

#include "esp_err.h"
#include "global_state.h"
#include "mining.h"

esp_err_t local_work_client_start(GlobalState *GLOBAL_STATE);
bool local_work_is_enabled(void);
esp_err_t local_work_submit_block(GlobalState *GLOBAL_STATE,
                                  const bm_job *job,
                                  uint32_t nonce,
                                  uint32_t rolled_version);

#endif // LOCAL_WORK_CLIENT_H_
