#ifndef BM1368_H_
#define BM1368_H_

#include "common.h"
#include "driver/gpio.h"
#include "mining.h"

#define ASIC_BM1368_JOB_FREQUENCY_MS 500

#define BM1368_ASIC_DIFFICULTY 256

#define BM1368_SERIALTX_DEBUG false
#define BM1368_SERIALRX_DEBUG false
#define BM1368_DEBUG_WORK false //causes insane amount of debug output
#define BM1368_DEBUG_JOBS false //causes insane amount of debug output

static const uint64_t BM1368_CORE_COUNT = 80;
static const uint64_t BM1368_SMALL_CORE_COUNT = 1276;
static const float BM1368_NONCE_PERCENT = BM1368_CORE_COUNT/256.0; //maximum nonce space with 1 chip
static const float BM1368_MIDSTATE_ENGINES = 16.0;
static const float BM1368_HCN = 5540.0;
static const float BM1368_HCN_MAX = 430000.0;
static const float BM1368_HCN_PERCENT = BM1368_HCN/BM1368_HCN_MAX;       //hcn limit effect
static const float BM1368_VERSION_PERCENT = 1.0;                   //version scan percent
static const float BM1368_FULLSCAN_PERCENT = 0.5;                  //normalised value 1 means do the maximum space, 0 dont wait before sending work

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

uint8_t BM1368_init(uint64_t frequency, uint16_t asic_count);
void BM1368_send_work(void * GLOBAL_STATE, bm_job * next_bm_job);
void BM1368_set_job_difficulty_mask(int);
void BM1368_set_version_mask(uint32_t version_mask);
int BM1368_set_max_baud(void);
int BM1368_set_default_baud(void);
void BM1368_send_hash_frequency(float frequency);
bool BM1368_set_frequency(float target_freq);
task_result * BM1368_process_work(void * GLOBAL_STATE);

#endif /* BM1368_H_ */
