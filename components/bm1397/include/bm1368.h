#ifndef BM1368_H_
#define BM1368_H_

#include "common.h"
#include "driver/gpio.h"
#include "mining.h"

#define CRC5_MASK 0x1F

// static const uint64_t BM1368_FREQUENCY = CONFIG_ASIC_FREQUENCY;
static const uint64_t BM1368_CORE_COUNT = 672;
// static const uint64_t BM1368_HASHRATE_S = BM1368_FREQUENCY * BM1368_CORE_COUNT * 1000000;
//  2^32
//  static const uint64_t NONCE_SPACE = 4294967296;
static const double BM1368_FULLSCAN_MS = 2140;

typedef struct
{
    float frequency;
} bm1368Module;

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
} BM1368_job;

uint8_t BM1368_init(uint64_t frequency);

uint8_t BM1368_send_init(void);
void BM1368_send_work(void * GLOBAL_STATE, bm_job * next_bm_job);
void BM1368_set_job_difficulty_mask(int);
int BM1368_set_max_baud(void);
int BM1368_set_default_baud(void);
void BM1368_send_hash_frequency(float frequency);
task_result * BM1368_proccess_work(void * GLOBAL_STATE);

#endif /* BM1368_H_ */
