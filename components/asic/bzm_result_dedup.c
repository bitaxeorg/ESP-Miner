#include "bzm/result_dedup.h"

#include <string.h>

#include "utils.h"

bool bzm_result_is_duplicate(bzm_result_dedup_t *cache,
                             const asic_job_t *job,
                             const bzm_result_t *result)
{
    if (cache == NULL || job == NULL || result == NULL) return false;
    size_t job_id_length = strnlen(job->job_id, sizeof(job->job_id));
    size_t extranonce_length = strnlen(job->extranonce2, sizeof(job->extranonce2));
    if (job_id_length == sizeof(job->job_id) ||
        extranonce_length == sizeof(job->extranonce2)) return false;

    // Handles, engines, sequence IDs and base rolling offsets are assignment
    // details. Hash the actual submitted header plus its pool/session identity.
    uint8_t identity[80 + 2 + sizeof(job->job_id) + sizeof(job->extranonce2)] = {0};
    asic_job_t resolved = *job;
    resolved.ntime = result->final_ntime;
    asic_job_header(&resolved, result->nonce, result->final_version, identity);
    identity[80] = job->pool_id;
    identity[81] = (uint8_t)job->source_type;
    memcpy(identity + 82, job->job_id, job_id_length);
    memcpy(identity + 82 + sizeof(job->job_id), job->extranonce2, extranonce_length);
    uint8_t fingerprint[32];
    double_sha256_bin(identity, sizeof(identity), fingerprint);
    for (size_t i = 0; i < BZM_RESULT_DEDUP_CAPACITY; ++i) {
        if (cache->entries[i].valid &&
            memcmp(cache->entries[i].fingerprint, fingerprint, sizeof(fingerprint)) == 0)
            return true;
    }
    bzm_result_identity_t *entry = &cache->entries[cache->next];
    entry->valid = true;
    memcpy(entry->fingerprint, fingerprint, sizeof(fingerprint));
    cache->next = (cache->next + 1) % BZM_RESULT_DEDUP_CAPACITY;
    return false;
}
