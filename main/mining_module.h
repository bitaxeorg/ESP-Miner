#ifndef MINING_MODULE_H_
#define MINING_MODULE_H_

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
//seems to be unused. get set from Kconfig
#define STRATUM_USER CONFIG_STRATUM_USER
#define FALLBACK_STRATUM_USER CONFIG_FALLBACK_STRATUM_USER

#define QUEUE_SIZE 12

/**
 * A structure representing a work queue.
 */
typedef struct
{
    /**
     * Buffer holding the elements of the queue.
     */
    void *buffer[QUEUE_SIZE];
    /**
     * Index of the front element in the circular buffer.
     */
    int head;
    /**
     * Index of the back element in the circular buffer.
     */
    int tail;
    /**
     * Current number of elements in the queue.
     */
    int count;
    /**
     * Mutex used to synchronize access to this work queue.
     */
    pthread_mutex_t lock;
    /**
     * Condition variable that signals when the queue is not empty.
     */
    pthread_cond_t not_empty;
    /**
     * Condition variable that signals when the queue is not full.
     */
    pthread_cond_t not_full;
} work_queue;

/**
 * A structure containing various mining-related queues and parameters.
 */
typedef struct{
    /**
     * Queue for stratum-related jobs.
     */
    work_queue stratum_queue;
    /**
     * Queue for ASIC job tasks.
     */
    work_queue ASIC_jobs_queue;
    /**
     * String to hold extra nonces used in mining.
     */
    char * extranonce_str;
    /**
     * Length of the second extra nonce.
     */
    int extranonce_2_len;
    /**
     * Flag to indicate if the current job should be abandoned.
     */
    int abandon_work;

    /**
     * Indicates if a new set mining difficulty message is available.
     */
    bool new_set_mining_difficulty_msg;
    /**
     * Version mask for stratum protocol handling.
     */
    uint32_t version_mask;
    /**
     * Indicates if there's a new stratum version rolling message.
     */
    bool new_stratum_version_rolling_msg;

}mining_queues;

extern mining_queues MINING_MODULE;
#endif