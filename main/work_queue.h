#ifndef WORK_QUEUE_H
#define WORK_QUEUE_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#define QUEUE_SIZE 12

typedef struct mining_notify mining_notify;
typedef struct sv2_job sv2_job_t;
typedef struct sv2_ext_job sv2_ext_job_t;

typedef enum
{
    WORK_ITEM_NONE = 0,
    WORK_ITEM_STRATUM_V1,
    WORK_ITEM_STRATUM_V2_STANDARD,
    WORK_ITEM_STRATUM_V2_EXTENDED,
} work_item_kind_t;

typedef union
{
    void *data;
    mining_notify *v1;
    sv2_job_t *sv2_standard;
    sv2_ext_job_t *sv2_extended;
} work_item_data_t;

typedef struct
{
    work_item_kind_t kind;
    // Generation of the active source. Items from an earlier generation are
    // stale even when the protocol kind is unchanged (for example clean_jobs).
    // The counter wraps naturally; zero is a valid generation.
    uint32_t source_epoch;
    work_item_data_t job;
    void (*free_fn)(void *);
} work_queue_item_t;

typedef struct
{
    work_item_kind_t kind;
    uint32_t epoch;
} work_queue_source_t;

typedef struct
{
    work_queue_item_t buffer[QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    work_item_kind_t source_kind;
    uint32_t source_epoch;
} work_queue;

void queue_init(work_queue *queue);
work_queue_item_t work_queue_item_create(work_queue *queue,
                                         work_item_kind_t kind,
                                         work_item_data_t job,
                                         void (*free_fn)(void *));
void work_queue_item_free(work_queue_item_t *item);
void queue_set_source(work_queue *queue, work_item_kind_t kind);
work_queue_source_t queue_get_source(work_queue *queue);
bool queue_enqueue(work_queue *queue, work_queue_item_t new_work);
work_queue_item_t queue_dequeue(work_queue *queue);
work_queue_item_t queue_dequeue_timeout(work_queue *queue, int timeout_ms);
void queue_clear(work_queue *queue);

#endif // WORK_QUEUE_H
