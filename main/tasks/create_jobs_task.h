#ifndef CREATE_JOBS_TASK_H_
#define CREATE_JOBS_TASK_H_

#include <stdint.h>

typedef struct GlobalState GlobalState;

#define CREATE_JOBS_NOTIFY_REFRESH UINT32_MAX

void create_jobs_refresh(GlobalState *state);
void create_jobs_task(void *pvParameters);

#endif
