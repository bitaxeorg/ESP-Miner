#include "mining.h"
#include "utils.h"
#include <string.h>

bool mining_build_asic_job(const miner_job_t *source, uint64_t extranonce2,
                           uint32_t version, asic_job_t *destination)
{
    if (source == NULL || destination == NULL ||
        memchr(source->job_id, 0, sizeof(source->job_id)) == NULL) {
        return false;
    }
    asic_job_t job = {
        .version = version != 0 ? version : source->version,
        .version_mask = source->version_mask,
        .ntime = source->ntime,
        .nbits = source->nbits,
        .pool_diff = source->pool_diff,
        .pool_id = source->pool_id,
        .source_type = source->type,
    };
    memcpy(job.prev_hash, source->prev_hash, sizeof(job.prev_hash));
    memcpy(job.job_id, source->job_id, sizeof(job.job_id));
    if (source->type == JOB_TYPE_SV2_STANDARD) {
        memcpy(job.merkle_root, source->merkle_root, sizeof(job.merkle_root));
    } else {
        size_t e2_len = source->extranonce2_len;
        if (e2_len > ASIC_JOB_EXTRANONCE2_SIZE) {
            return false;
        }
        uint8_t e2[ASIC_JOB_EXTRANONCE2_SIZE] = {0};
        for (size_t i = 0; i < e2_len && i < sizeof(extranonce2); ++i) {
            e2[i] = (uint8_t)(extranonce2 >> (8 * i));
        }
        bin2hex(e2, e2_len, job.extranonce2, sizeof(job.extranonce2));
        uint8_t coinbase_hash[32];
        calculate_coinbase_tx_hash_bin(source->coinbase_prefix,
            source->coinbase_prefix_len, source->extranonce1,
            source->extranonce1_len, e2, e2_len, source->coinbase_suffix,
            source->coinbase_suffix_len, coinbase_hash);
        calculate_merkle_root_hash(coinbase_hash,
            (const uint8_t (*)[32])source->merkle_path,
            source->merkle_path_count, job.merkle_root);
    }
    *destination = job;
    return true;
}
