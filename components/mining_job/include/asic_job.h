#ifndef ASIC_JOB_H_
#define ASIC_JOB_H_

#include <stdint.h>

#define ASIC_JOB_ID_LEN 32
#define ASIC_JOB_EXTRANONCE2_SIZE 32
#define ASIC_JOB_EXTRANONCE2_HEX_SIZE (ASIC_JOB_EXTRANONCE2_SIZE * 2 + 1)

typedef enum {
    JOB_TYPE_V1 = 0,
    JOB_TYPE_SV2_STANDARD,
    JOB_TYPE_SV2_EXTENDED,
} mining_job_source_t;

/* Complete owned work. Hash arrays contain the exact bytes in the Bitcoin
 * header; integers use host byte order. Metadata strings are terminated.
 * No member points into a parser, coinbase buffer, or hardware job slot. */
typedef struct asic_job {
    uint32_t version;
    uint32_t version_mask;
    uint8_t prev_hash[32];
    uint8_t merkle_root[32];
    uint32_t ntime;
    uint32_t nbits;
    uint32_t starting_nonce;
    double pool_diff;
    uint8_t pool_id;
    mining_job_source_t source_type;
    char job_id[ASIC_JOB_ID_LEN];
    char extranonce2[ASIC_JOB_EXTRANONCE2_HEX_SIZE];
} asic_job_t;

/* The caller supplies valid work and an 80-byte output buffer. Encode all
 * integers explicitly as little endian, independent of the host CPU. */
void asic_job_header(const asic_job_t *job, uint32_t nonce,
                     uint32_t version, uint8_t header[80]);

#endif
