#ifndef WORK_QUEUE_H
#define WORK_QUEUE_H

#include <pthread.h>
#include "mining.h"

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

void queue_init(work_queue *queue);
void queue_enqueue(work_queue *queue, void *new_work);
void *queue_dequeue(work_queue *queue);
void *queue_dequeue_timeout(work_queue *queue, int timeout_ms);
void queue_clear(work_queue *queue);

#endif // WORK_QUEUE_H