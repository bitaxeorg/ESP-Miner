#ifndef BM1397_H_
#define BM1397_H_

#include "asic_common.h"
#include "asic_job.h"

typedef struct GlobalState GlobalState;

#define BM1397_SERIALTX_DEBUG false
#define BM1397_SERIALRX_DEBUG false
#define BM1397_DEBUG_WORK false //causes insane amount of debug output
#define BM1397_DEBUG_JOBS false //causes insane amount of debug output

uint8_t BM1397_init(GlobalState * GLOBAL_STATE);
void BM1397_send_work(GlobalState * GLOBAL_STATE, asic_job_t * next_job);
void BM1397_set_version_mask(uint32_t version_mask);
int BM1397_set_max_baud(void);
float BM1397_send_hash_frequency(float frequency);
task_result * BM1397_process_work(GlobalState * GLOBAL_STATE);
void BM1397_read_registers(GlobalState * GLOBAL_STATE);

#endif /* BM1397_H_ */
