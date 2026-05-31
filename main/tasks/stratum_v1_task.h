#ifndef STRATUM_V1_TASK_H_
#define STRATUM_V1_TASK_H_

#include <stdbool.h>
#include "global_state.h"

void stratum_v1_task(void *pvParameters);
void stratum_v1_close_connection(GlobalState *GLOBAL_STATE);
void stratum_v1_record_share_block_candidate(int request_id, bool is_block_candidate, double diff);

#endif // STRATUM_V1_TASK_H_
