#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <pthread.h>
#include "mining.h"
typedef struct
{
    // ASIC may not return the nonce in the same order as the jobs were sent
    // it also may return a previous nonce under some circumstances
    // so we keep a list of jobs indexed by the job id
    bm_job **active_jobs;
    //semaphone
    SemaphoreHandle_t semaphore;
    pthread_mutex_t valid_jobs_lock;
    uint8_t * valid_jobs;
} AsicTaskModule;

extern AsicTaskModule ASIC_TASK_MODULE;