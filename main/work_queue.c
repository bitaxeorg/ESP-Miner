#include "work_queue.h"
#include "esp_log.h"
#include <stdlib.h>
#include <time.h>

void queue_init(work_queue *queue)
{
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->source_kind = WORK_ITEM_NONE;
    queue->source_epoch = 1;
    pthread_mutex_init(&queue->lock, NULL);
    pthread_cond_init(&queue->not_empty, NULL);
    pthread_cond_init(&queue->not_full, NULL);
}

work_queue_item_t work_queue_item_create(work_queue *queue, void *data,
                                         work_item_kind_t kind,
                                         void (*free_fn)(void *))
{
    work_queue_item_t item = {
        .data = data,
        .kind = kind,
        .free_fn = free_fn,
    };

    pthread_mutex_lock(&queue->lock);
    item.source_epoch = queue->source_epoch;
    pthread_mutex_unlock(&queue->lock);
    return item;
}

void work_queue_item_free(work_queue_item_t *item)
{
    if (item == NULL) {
        return;
    }

    if (item->data != NULL) {
        if (item->free_fn != NULL) {
            item->free_fn(item->data);
        } else {
            free(item->data);
        }
    }

    *item = (work_queue_item_t) {0};
}

static void queue_clear_locked(work_queue *queue)
{
    while (queue->count > 0) {
        work_queue_item_t next_work = queue->buffer[queue->head];
        work_queue_item_free(&next_work);
        queue->head = (queue->head + 1) % QUEUE_SIZE;
        queue->count--;
    }
}

void queue_set_source(work_queue *queue, work_item_kind_t kind)
{
    pthread_mutex_lock(&queue->lock);
    queue_clear_locked(queue);
    queue->source_kind = kind;
    queue->source_epoch++;
    if (queue->source_epoch == 0) {
        queue->source_epoch = 1;
    }
    pthread_cond_broadcast(&queue->not_empty);
    pthread_cond_broadcast(&queue->not_full);
    pthread_mutex_unlock(&queue->lock);
}

work_queue_source_t queue_get_source(work_queue *queue)
{
    pthread_mutex_lock(&queue->lock);
    work_queue_source_t source = {
        .kind = queue->source_kind,
        .epoch = queue->source_epoch,
    };
    pthread_mutex_unlock(&queue->lock);
    return source;
}

bool queue_enqueue(work_queue *queue, work_queue_item_t new_work)
{
    pthread_mutex_lock(&queue->lock);

    if (queue->source_kind == WORK_ITEM_NONE ||
        new_work.kind != queue->source_kind ||
        new_work.source_epoch != queue->source_epoch) {
        pthread_mutex_unlock(&queue->lock);
        work_queue_item_free(&new_work);
        return false;
    }

    if (queue->count == QUEUE_SIZE) {
        work_queue_item_t old_work = queue->buffer[queue->head];
        work_queue_item_free(&old_work);
        queue->head = (queue->head + 1) % QUEUE_SIZE;
        queue->count--;
    }

    queue->buffer[queue->tail] = new_work;
    queue->tail = (queue->tail + 1) % QUEUE_SIZE;
    queue->count++;

    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->lock);
    return true;
}

work_queue_item_t queue_dequeue(work_queue *queue)
{
    pthread_mutex_lock(&queue->lock);

    while (queue->count == 0)
    {
        pthread_cond_wait(&queue->not_empty, &queue->lock);
    }

    work_queue_item_t next_work = queue->buffer[queue->head];
    queue->head = (queue->head + 1) % QUEUE_SIZE;
    queue->count--;

    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->lock);

    return next_work;
}

work_queue_item_t queue_dequeue_timeout(work_queue *queue, int timeout_ms)
{
    pthread_mutex_lock(&queue->lock);

    if (queue->count == 0 && timeout_ms <= 0) {
        pthread_mutex_unlock(&queue->lock);
        return (work_queue_item_t) {0};
    }

    uint32_t starting_epoch = queue->source_epoch;
    struct timespec timeout_time;
    clock_gettime(CLOCK_REALTIME, &timeout_time);
    timeout_time.tv_sec += timeout_ms / 1000;
    timeout_time.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (timeout_time.tv_nsec >= 1000000000L) {
        timeout_time.tv_sec += 1;
        timeout_time.tv_nsec -= 1000000000L;
    }

    while (queue->count == 0) {
        int result = pthread_cond_timedwait(&queue->not_empty, &queue->lock, &timeout_time);
        if (queue->source_epoch != starting_epoch || result != 0) {
            pthread_mutex_unlock(&queue->lock);
            return (work_queue_item_t) {0};
        }
    }

    work_queue_item_t next_work = queue->buffer[queue->head];
    queue->head = (queue->head + 1) % QUEUE_SIZE;
    queue->count--;

    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->lock);

    return next_work;
}

void queue_clear(work_queue *queue)
{
    queue_set_source(queue, WORK_ITEM_NONE);
}
