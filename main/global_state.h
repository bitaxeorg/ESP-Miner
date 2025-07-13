#ifndef GLOBAL_STATE_H_
#define GLOBAL_STATE_H_

#include "common.h"
#include "device_config.h"
#include "display.h"
#include "serial.h"
#include "statistics_task.h"
#include "stratum_api.h"
#include "work_queue.h"
#include <stdbool.h>
#include <stdint.h>

#define STRATUM_USER CONFIG_STRATUM_USER
#define FALLBACK_STRATUM_USER CONFIG_FALLBACK_STRATUM_USER

#define HISTORY_LENGTH 100
#define DIFF_STRING_SIZE 10

typedef struct
{
    char message[64];
    uint32_t count;
} RejectedReasonStat;


typedef struct
{
    work_queue stratum_queue;
    work_queue ASIC_jobs_queue;

    char * extranonce_str;
    int extranonce_2_len;
    int abandon_work;

    uint32_t pool_difficulty;
    bool new_set_mining_difficulty_msg;
    uint32_t version_mask;
    bool new_stratum_version_rolling_msg;

    int sock;

    // A message ID that must be unique per request that expects a response.
    // For requests not expecting a response (called notifications), this is null.
    int send_uid;

    bool ASIC_initalized;
    bool psram_is_available;
} GlobalState;

extern GlobalState GLOBAL_STATE;



#endif /* GLOBAL_STATE_H_ */
