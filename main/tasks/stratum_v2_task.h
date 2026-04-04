#ifndef STRATUM_V2_TASK_H
#define STRATUM_V2_TASK_H

#include <stdbool.h>
#include "global_state.h"

void stratum_v2_task(void *pvParameters);
void stratum_v2_close_connection(GlobalState *GLOBAL_STATE);
int stratum_v2_submit_share(GlobalState *GLOBAL_STATE, uint32_t job_id, uint32_t nonce,
                            uint32_t ntime, uint32_t version);
int stratum_v2_submit_share_extended(GlobalState *GLOBAL_STATE, uint32_t job_id,
                                     uint32_t nonce, uint32_t ntime, uint32_t version,
                                     const uint8_t *extranonce, uint8_t extranonce_len);
bool stratum_v2_is_extended_channel(GlobalState *GLOBAL_STATE);

#endif // STRATUM_V2_TASK_H
