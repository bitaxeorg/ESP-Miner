#pragma once

//seems to be unused. get set from Kconfig
#define STRATUM_USER CONFIG_STRATUM_USER
#define FALLBACK_STRATUM_USER CONFIG_FALLBACK_STRATUM_USER

#define QUEUE_SIZE 12

typedef struct
{
    void *buffer[QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} work_queue;

typedef struct{
    work_queue stratum_queue;
    work_queue ASIC_jobs_queue;
    char * extranonce_str;
    int extranonce_2_len;
    int abandon_work;

    bool new_set_mining_difficulty_msg;
    uint32_t version_mask;
    bool new_stratum_version_rolling_msg;

    //maybe need a stratum module
    int sock;

    // A message ID that must be unique per request that expects a response.
    // For requests not expecting a response (called notifications), this is null.
    int send_uid;
}mining_queues;

extern mining_queues MINING_MODULE;