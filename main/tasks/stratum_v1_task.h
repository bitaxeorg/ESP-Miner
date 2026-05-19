#ifndef STRATUM_V1_TASK_H_
#define STRATUM_V1_TASK_H_

#include "global_state.h"

void stratum_v1_task(void *pvParameters);
void stratum_v1_close_connection(GlobalState *GLOBAL_STATE);

#endif // STRATUM_V1_TASK_H_
