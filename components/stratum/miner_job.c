#include "miner_job.h"
#include "stratum_api.h"
#include "utils.h"
#include <string.h>
#include <pthread.h>

static miner_job_t s_job_pool[MINER_JOB_POOL_SIZE];
static uint32_t s_pool_head = 0;
static pthread_mutex_t s_pool_lock = PTHREAD_MUTEX_INITIALIZER;

void miner_job_pool_init(void)
{
    pthread_mutex_lock(&s_pool_lock);
    memset(s_job_pool, 0, sizeof(s_job_pool));
    s_pool_head = 0;
    pthread_mutex_unlock(&s_pool_lock);
}

miner_job_t *miner_job_pool_next(void)
{
    pthread_mutex_lock(&s_pool_lock);
    miner_job_t *slot = &s_job_pool[s_pool_head % MINER_JOB_POOL_SIZE];
    s_pool_head++;
    memset(slot, 0, sizeof(miner_job_t));
    pthread_mutex_unlock(&s_pool_lock);
    return slot;
}
