#ifndef BZM_JOB_STORE_H
#define BZM_JOB_STORE_H

#include <stdbool.h>
#include <pthread.h>
#include <stdint.h>

#include "bzm/runtime_result.h"
#include "asic_job.h"

/* Preserve the full 8-bit handle slot space for current and delayed work;
 * production row multicast shares a single stored job across each row. */
#define BZM_JOB_STORE_CAPACITY 256

typedef struct {
    bool valid;
    bzm_work_handle_t handle;
    asic_job_t template;
} bzm_job_store_entry_t;

typedef struct {
    pthread_mutex_t lock;
    bzm_job_store_entry_t *entries;
    uint16_t capacity;
    uint16_t next_slot;
    uint64_t next_generation;
} bzm_job_store_t;

bool bzm_job_store_init(bzm_job_store_t *store);
bool bzm_job_store_init_with_memory(bzm_job_store_t *store, bool large);
void bzm_job_store_destroy(bzm_job_store_t *store);

// Generation-bearing handles reject stale results after reuse or invalidation.
bool bzm_job_store_store_generated(bzm_job_store_t *store,
                                    const asic_job_t *template,
                                    bzm_work_handle_t *handle);

bool bzm_job_store_snapshot(bzm_job_store_t *store,
                             bzm_work_handle_t handle,
                             asic_job_t *snapshot);
// Read-only identity check used by drivers before emitting a delayed result.
bool bzm_job_store_contains(bzm_job_store_t *store,
                             bzm_work_handle_t handle);
bool bzm_job_store_release(bzm_job_store_t *store,
                            bzm_work_handle_t handle);
void bzm_job_store_invalidate_all(bzm_job_store_t *store);

#endif // BZM_JOB_STORE_H
