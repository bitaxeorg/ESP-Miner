#ifndef ASIC_JOB_SLOT_H_
#define ASIC_JOB_SLOT_H_

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "asic_job.h"

typedef struct asic_job_slot_entry
{
    bool is_valid;
    asic_job_t job;
} asic_job_slot_entry_t;

typedef struct asic_job_store
{
    asic_job_slot_entry_t *entries;
    uint16_t capacity;
    uint16_t current_slot;
    pthread_mutex_t lock;
} asic_job_store_t;

void asic_job_store_init(asic_job_store_t *store, asic_job_slot_entry_t *entries, uint16_t capacity);
void asic_job_store_destroy(asic_job_store_t *store);
uint16_t asic_job_store_claim(asic_job_store_t *store, const asic_job_t *job);
bool asic_job_store_lookup(asic_job_store_t *store, uint16_t slot, asic_job_t *out_job);
void asic_job_store_invalidate_all(asic_job_store_t *store);

#endif /* ASIC_JOB_SLOT_H_ */
