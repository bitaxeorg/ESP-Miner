#ifndef BM1373_H_
#define BM1373_H_

#include "asic_common.h"
#include "mining.h"

#define BM1373_SERIALTX_DEBUG false
#define BM1373_SERIALRX_DEBUG false
#define BM1373_DEBUG_WORK false //causes insane amount of debug output
#define BM1373_DEBUG_JOBS false //causes insane amount of debug output

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
} BM1373_job;

uint8_t BM1373_init(void * GLOBAL_STATE);
void BM1373_send_work(void * GLOBAL_STATE, bm_job * next_bm_job);
void BM1373_set_version_mask(uint32_t version_mask);
int BM1373_set_max_baud(void);
int BM1373_set_default_baud(void);
float BM1373_send_hash_frequency(float frequency);
task_result * BM1373_process_work(void * GLOBAL_STATE);
void BM1373_read_registers(void);
void BM1373_set_nonce_space(double nonce_percent, float frequency, uint16_t asic_count, uint16_t cores);

#endif /* BM1373_H_ */
