#ifndef WORK_QUEUE_H
#define WORK_QUEUE_H

#include <pthread.h>

#define QUEUE_SIZE 10

#define MAX_MERKLE_BRANCHES 32
#define PREV_BLOCK_HASH_SIZE 32
#define COINBASE_SIZE 100

typedef struct {
    uint32_t job_id;
    uint8_t prev_block_hash[PREV_BLOCK_HASH_SIZE];
    uint8_t coinbase[COINBASE_SIZE];
    size_t coinbase_len;
    uint8_t merkle_branches[MAX_MERKLE_BRANCHES][PREV_BLOCK_HASH_SIZE];
    size_t n_merkle_branches;
    uint32_t version;
    uint32_t curtime;
    uint32_t bits;
    uint32_t target;
    uint32_t nonce;
} work;

typedef struct {
    work buffer[QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} work_queue;

void queue_init(work_queue *queue);
void queue_enqueue(work_queue *queue, work new_work);
work queue_dequeue(work_queue *queue, int *termination_flag);

#endif // WORK_QUEUE_H