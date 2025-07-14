#ifndef WORK_QUEUE_H
#define WORK_QUEUE_H

#include <pthread.h>
#include "mining.h"
#include "mining_module.h"


void queue_init(work_queue *queue);
void queue_enqueue(work_queue *queue, void *new_work);
void ASIC_jobs_queue_clear(work_queue *queue);
void *queue_dequeue(work_queue *queue);
void queue_clear(work_queue *queue);

#endif // WORK_QUEUE_H