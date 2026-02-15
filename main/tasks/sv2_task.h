#ifndef SV2_TASK_H
#define SV2_TASK_H

#include "global_state.h"

void sv2_task(void *pvParameters);
void sv2_close_connection(GlobalState *GLOBAL_STATE);
int sv2_submit_share(GlobalState *GLOBAL_STATE, uint32_t job_id, uint32_t nonce,
                     uint32_t ntime, uint32_t version);

#endif // SV2_TASK_H
