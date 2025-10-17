#ifndef BM1397_H_
#define BM1397_H_

#include "common.h"
#include "mining.h"

#define BM1397_SERIALTX_DEBUG false
#define BM1397_SERIALRX_DEBUG false
#define BM1397_DEBUG_WORK false //causes insane amount of debug output
#define BM1397_DEBUG_JOBS false //causes insane amount of debug output

typedef struct __attribute__((__packed__))
{
    uint8_t job_id;
    uint8_t num_midstates;
    uint8_t starting_nonce[4];
    uint8_t nbits[4];
    uint8_t ntime[4];
    uint8_t merkle4[4];
    uint8_t midstate[32];
    uint8_t midstate1[32];
    uint8_t midstate2[32];
    uint8_t midstate3[32];
} job_packet;

uint8_t BM1397_init(float frequency, uint16_t asic_count, uint16_t difficulty);
void BM1397_send_work(void * GLOBAL_STATE, bm_job * next_bm_job);
void BM1397_set_version_mask(uint32_t version_mask);
int BM1397_set_max_baud(void);
int BM1397_set_default_baud(void);
void BM1397_send_hash_frequency(float frequency);
task_result * BM1397_process_work(void * GLOBAL_STATE);
void BM1397_read_registers(void);

#endif /* BM1397_H_ */
