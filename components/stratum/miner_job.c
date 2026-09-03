#include "miner_job.h"
#include "stratum_api.h"
#include "utils.h"
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

static miner_job_t s_job_pool[MINER_JOB_POOL_SIZE];

void miner_job_pool_init(void)
{
    for (size_t i = 0; i < MINER_JOB_POOL_SIZE; i++) {
        if (!s_job_pool[i].coinbase_prefix) {
            s_job_pool[i].coinbase_prefix = heap_caps_calloc(1, MAX_COINBASE_PREFIX_LEN, MALLOC_CAP_SPIRAM);
            if (!s_job_pool[i].coinbase_prefix) {
                s_job_pool[i].coinbase_prefix = calloc(1, MAX_COINBASE_PREFIX_LEN);
            }
        }
        if (!s_job_pool[i].coinbase_suffix) {
            s_job_pool[i].coinbase_suffix = heap_caps_calloc(1, MAX_COINBASE_SUFFIX_LEN, MALLOC_CAP_SPIRAM);
            if (!s_job_pool[i].coinbase_suffix) {
                s_job_pool[i].coinbase_suffix = calloc(1, 2048);
            }
        }
        uint8_t *p_buf = s_job_pool[i].coinbase_prefix;
        uint8_t *s_buf = s_job_pool[i].coinbase_suffix;
        memset(&s_job_pool[i], 0, sizeof(miner_job_t));
        s_job_pool[i].coinbase_prefix = p_buf;
        s_job_pool[i].coinbase_suffix = s_buf;
    }
}

miner_job_t *miner_job_get_slot(size_t index)
{
    size_t slot_idx = index % MINER_JOB_POOL_SIZE;
    if (!s_job_pool[slot_idx].coinbase_prefix || !s_job_pool[slot_idx].coinbase_suffix) {
        miner_job_pool_init();
    }
    return &s_job_pool[slot_idx];
}
