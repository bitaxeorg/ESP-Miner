#ifndef BM1368_H_
#define BM1368_H_

#include "asic_common.h"
#include "asic_job.h"

typedef struct GlobalState GlobalState;

#define BM1368_SERIALTX_DEBUG false
#define BM1368_SERIALRX_DEBUG false
#define BM1368_DEBUG_WORK false //causes insane amount of debug output
#define BM1368_DEBUG_JOBS false //causes insane amount of debug output

uint8_t BM1368_init(GlobalState * GLOBAL_STATE);
void BM1368_send_work(GlobalState * GLOBAL_STATE, asic_job_t * next_job);
void BM1368_set_version_mask(uint32_t version_mask);
int BM1368_set_max_baud(void);
float BM1368_send_hash_frequency(float frequency);
task_result * BM1368_process_work(GlobalState * GLOBAL_STATE);
void BM1368_read_registers(GlobalState * GLOBAL_STATE);
void BM1368_set_nonce_space(double nonce_percent, float frequency, uint16_t asic_count, uint16_t cores);

#endif /* BM1368_H_ */
