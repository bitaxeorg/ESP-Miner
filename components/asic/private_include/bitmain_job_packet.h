#ifndef BITMAIN_JOB_PACKET_H_
#define BITMAIN_JOB_PACKET_H_

#include "asic_job.h"

#define BM1397_NUM_MIDSTATES 4

/* Wire payload shared by BM1366, BM1368, BM1370, and BM1373. All integers
 * are byte arrays so encoding also works at unaligned packet addresses. */
typedef struct __attribute__((__packed__)) {
    uint8_t job_id;
    uint8_t num_midstates;
    uint8_t starting_nonce[4];
    uint8_t nbits[4];
    uint8_t ntime[4];
    uint8_t merkle_root[32];
    uint8_t prev_block_hash[32];
    uint8_t version[4];
} bm13xx_job_packet_t;

typedef struct __attribute__((__packed__)) {
    uint8_t job_id;
    uint8_t num_midstates;
    uint8_t starting_nonce[4];
    uint8_t nbits[4];
    uint8_t ntime[4];
    uint8_t merkle4[4];
    uint8_t midstates[BM1397_NUM_MIDSTATES][32];
} bm1397_job_packet_t;

/* Encode directly into the final wire payload. The caller supplies valid,
 * non-overlapping job and packet storage; neither function retains pointers. */
void bm13xx_build_job_packet(const asic_job_t *job, uint8_t job_id,
                             bm13xx_job_packet_t *packet);
void bm1397_build_job_packet(const asic_job_t *job, uint8_t job_id,
                             uint8_t software_midstates,
                             bm1397_job_packet_t *packet);

#endif
