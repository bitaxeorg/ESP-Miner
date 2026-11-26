#ifndef CREATE_JOBS_TASK_H_
#define CREATE_JOBS_TASK_H_

#include <stdint.h>

#define CREATE_JOBS_NOTIFY_CLEAR_WORK UINT32_MAX
#define CREATE_JOBS_NOTIFY_REFRESH    (UINT32_MAX - 1U)

void create_jobs_task(void *pvParameters);

#endif
