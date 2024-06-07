#ifndef GLOBAL_STATE_H_
#define GLOBAL_STATE_H_

#include "asic_task.h"
#include "bm1368.h"
#include "bm1366.h"
#include "bm1397.h"
#include "common.h"
#include "power_management_task.h"
#include "serial.h"
#include "stratum_api.h"
#include "system.h"
#include "work_queue.h"

#define STRATUM_USER CONFIG_STRATUM_USER

typedef enum
{
    DEVICE_MAX = 0,
    DEVICE_ULTRA,
    DEVICE_SUPRA,
} DeviceModel;

typedef enum
{
    ASIC_BM1397 = 0,
    ASIC_BM1366,
    ASIC_BM1368,
} AsicModel;

typedef struct
{
    uint8_t (*init_fn)(uint64_t);
    task_result * (*receive_result_fn)(void * GLOBAL_STATE);
    int (*set_max_baud_fn)(void);
    void (*set_difficulty_mask_fn)(int);
    void (*send_work_fn)(void * GLOBAL_STATE, bm_job * next_bm_job);
} AsicFunctions;

typedef struct
{
    DeviceModel device_model;
    char * device_model_str;
    AsicModel asic_model;
    char * asic_model_str;
    AsicFunctions ASIC_functions;
    double asic_job_frequency_ms;
    uint32_t initial_ASIC_difficulty;

    work_queue stratum_queue;
    work_queue ASIC_jobs_queue;

    bm1397Module BM1397_MODULE;
    SystemModule SYSTEM_MODULE;
    AsicTaskModule ASIC_TASK_MODULE;
    PowerManagementModule POWER_MANAGEMENT_MODULE;

    char * extranonce_str;
    int extranonce_2_len;
    int abandon_work;

    uint8_t * valid_jobs;
    pthread_mutex_t valid_jobs_lock;

    uint32_t stratum_difficulty;
    uint32_t version_mask;

    int sock;

} GlobalState;

#endif /* GLOBAL_STATE_H_ */