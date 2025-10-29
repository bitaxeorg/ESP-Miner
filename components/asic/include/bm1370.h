#ifndef BM1370_H_
#define BM1370_H_

#include "common.h"
#include "mining.h"

#define BM1370_SERIALTX_DEBUG false
#define BM1370_SERIALRX_DEBUG false
#define BM1370_DEBUG_WORK false //causes insane amount of debug output
#define BM1370_DEBUG_JOBS false //causes insane amount of debug output

typedef struct __attribute__((__packed__))
{
    uint8_t job_id;
    uint8_t num_midstates;
    uint8_t starting_nonce[4];
    uint8_t nbits[4];
    uint8_t ntime[4];
    uint8_t merkle_root[32];
    uint8_t prev_block_hash[32];
    uint8_t version[4];
} BM1370_job;

uint8_t BM1370_init(float frequency, uint16_t asic_count, uint16_t difficulty);
void BM1370_send_work(void * GLOBAL_STATE, bm_job * next_bm_job);
void BM1370_set_version_mask(uint32_t version_mask);
int BM1370_set_max_baud(void);
int BM1370_set_default_baud(void);
void BM1370_send_hash_frequency(float frequency);
task_result * BM1370_process_work(void * GLOBAL_STATE);
void BM1370_read_registers(void);

#endif /* BM1370_H_ */
