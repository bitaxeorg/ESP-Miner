#ifndef BZM_RESULT_DEDUP_H
#define BZM_RESULT_DEDUP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "asic_job.h"
#include "bzm/result.h"

#define BZM_RESULT_DEDUP_CAPACITY 256U

typedef struct {
    bool valid;
    uint8_t fingerprint[32];
} bzm_result_identity_t;

typedef struct {
    bzm_result_identity_t entries[BZM_RESULT_DEDUP_CAPACITY];
    size_t next;
} bzm_result_dedup_t;

// Caller serializes access and zeroes the cache at logical work boundaries.
bool bzm_result_is_duplicate(bzm_result_dedup_t *cache,
                             const asic_job_t *job,
                             const bzm_result_t *result);

#endif
