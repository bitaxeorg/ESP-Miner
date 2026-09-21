#ifndef BZM_DRIVER_H
#define BZM_DRIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "asic_common.h"
#include "bzm/chain.h"

typedef struct GlobalState GlobalState;

/* The large Bonanza reactor and transport state is created only after board
 * detection selects BZM, and must reside in external RAM. */
bool BZM_driver_state_init(GlobalState *state);

int BZM_set_max_baud(void);
void BZM_submit_job(GlobalState *state, const asic_job_t *job);
bool BZM_clear_work(GlobalState * state);
double BZM_job_frequency_ms(GlobalState *state);
task_result * BZM_process_work(GlobalState * state);
float BZM_read_temperature(GlobalState * state);
bool BZM_hashrate_counter_snapshot(GlobalState *state,
                                   uint32_t *difficulty_one_counters,
                                   size_t counter_count);

#endif // BZM_DRIVER_H
