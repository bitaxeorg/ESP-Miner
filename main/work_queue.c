#include "work_queue.h"

void queue_init(work_queue *queue) {
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    pthread_mutex_init(&queue->lock, NULL);
    pthread_cond_init(&queue->not_empty, NULL);
    pthread_cond_init(&queue->not_full, NULL);
}

void queue_enqueue(work_queue *queue, char * new_work) {
    pthread_mutex_lock(&queue->lock);

    while (queue->count == QUEUE_SIZE) {
        pthread_cond_wait(&queue->not_full, &queue->lock);
    }

    queue->buffer[queue->tail] = new_work;
    queue->tail = (queue->tail + 1) % QUEUE_SIZE;
    queue->count++;

    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->lock);
}

char * queue_dequeue(work_queue *queue, int *termination_flag) {
    pthread_mutex_lock(&queue->lock);

    while (queue->count == 0) {
        if (*termination_flag) {
            pthread_mutex_unlock(&queue->lock);
            return NULL;
        }
        pthread_cond_wait(&queue->not_empty, &queue->lock);
    }

    char * next_work = queue->buffer[queue->head];
    queue->head = (queue->head + 1) % QUEUE_SIZE;
    queue->count--;

    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->lock);

    return next_work;
}