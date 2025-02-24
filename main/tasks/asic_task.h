#ifndef ASIC_TASK_H_
#define ASIC_TASK_H_

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mining.h"
typedef struct
{
    // ASIC may not return the nonce in the same order as the jobs were sent
    // it also may return a previous nonce under some circumstances
    // so we keep a list of jobs indexed by the job id
    bm_job **active_jobs;
    //semaphone
    SemaphoreHandle_t semaphore;
} AsicTaskModule;

void ASIC_task(void *pvParameters);

#endif /* ASIC_TASK_H_ */
