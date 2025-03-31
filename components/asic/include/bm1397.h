#ifndef BM1397_H_
#define BM1397_H_

#include "common.h"
#include "driver/gpio.h"
#include "mining.h"

#define ASIC_BM1397_JOB_FREQUENCY_MS 20 //not currently used

#define CRC5_MASK 0x1F
#define BM1397_ASIC_DIFFICULTY 256

#define BM1937_SERIALTX_DEBUG false
#define BM1937_SERIALRX_DEBUG false
#define BM1397_DEBUG_WORK false //causes insane amount of debug output
#define BM1397_DEBUG_JOBS false //causes insane amount of debug output

static const uint64_t BM1397_CORE_COUNT = 168;
static const uint64_t BM1397_SMALL_CORE_COUNT = 672;

typedef struct
{
    float frequency;
} bm1397Module;

typedef enum
{
    JOB_PACKET = 0,
    CMD_PACKET = 1,
} packet_type_t;

typedef enum
{
    JOB_RESP = 0,
    CMD_RESP = 1,
} response_type_t;

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

uint8_t BM1397_init(uint64_t frequency, uint16_t asic_count);

void BM1397_send_work(void * GLOBAL_STATE, bm_job * next_bm_job);
void BM1397_set_job_difficulty_mask(int);
void BM1397_set_version_mask(uint32_t version_mask);
int BM1397_set_max_baud(void);
int BM1397_set_default_baud(void);
void BM1397_send_hash_frequency(float frequency);
task_result * BM1397_proccess_work(void * GLOBAL_STATE);

#endif /* BM1397_H_ */