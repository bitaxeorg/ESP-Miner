#include "miner_job.h"
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include "esp_heap_caps.h"

static miner_job_pool_t s_default_job_pool;

void miner_job_pool_init_instance(miner_job_pool_t *pool)
{
    if (!pool) return;

    if (!pool->slots) {
        pool->slots = heap_caps_calloc(MINER_JOB_POOL_SIZE, sizeof(miner_job_t), MALLOC_CAP_SPIRAM);
        if (!pool->slots) {
            pool->slots = calloc(MINER_JOB_POOL_SIZE, sizeof(miner_job_t));
        }
        if (!pool->slots) {
            return;
        }
    }

    for (size_t i = 0; i < MINER_JOB_POOL_SIZE; i++) {
        if (!pool->slots[i].coinbase_prefix) {
            pool->slots[i].coinbase_prefix = heap_caps_calloc(1, MAX_COINBASE_PREFIX_LEN, MALLOC_CAP_SPIRAM);
            if (!pool->slots[i].coinbase_prefix) {
                pool->slots[i].coinbase_prefix = calloc(1, MAX_COINBASE_PREFIX_LEN);
            }
        }
        if (!pool->slots[i].coinbase_suffix) {
            pool->slots[i].coinbase_suffix = heap_caps_calloc(1, MAX_COINBASE_SUFFIX_LEN, MALLOC_CAP_SPIRAM);
            if (!pool->slots[i].coinbase_suffix) {
                pool->slots[i].coinbase_suffix = calloc(1, 2048);
            }
        }
        uint8_t *p_buf = pool->slots[i].coinbase_prefix;
        uint8_t *s_buf = pool->slots[i].coinbase_suffix;
        memset(&pool->slots[i], 0, sizeof(miner_job_t));
        pool->slots[i].coinbase_prefix = p_buf;
        pool->slots[i].coinbase_suffix = s_buf;
    }
}

miner_job_t *miner_job_pool_get_slot(miner_job_pool_t *pool, size_t index)
{
    if (!pool) return NULL;
    size_t slot_idx = index % MINER_JOB_POOL_SIZE;
    if (!pool->slots || !pool->slots[slot_idx].coinbase_prefix || !pool->slots[slot_idx].coinbase_suffix) {
        miner_job_pool_init_instance(pool);
    }
    if (!pool->slots) return NULL;
    return &pool->slots[slot_idx];
}

void miner_job_pool_free(miner_job_pool_t *pool)
{
    if (!pool || !pool->slots) return;
    for (size_t i = 0; i < MINER_JOB_POOL_SIZE; i++) {
        free(pool->slots[i].coinbase_prefix);
        free(pool->slots[i].coinbase_suffix);
    }
    free(pool->slots);
    pool->slots = NULL;
}

void miner_job_pool_init(void)
{
    miner_job_pool_init_instance(&s_default_job_pool);
}

miner_job_t *miner_job_get_slot(size_t index)
{
    return miner_job_pool_get_slot(&s_default_job_pool, index);
}
