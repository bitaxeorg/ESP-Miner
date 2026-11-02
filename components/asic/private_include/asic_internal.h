#ifndef ASIC_INTERNAL_H_
#define ASIC_INTERNAL_H_

#include "asic_job.h"

typedef struct GlobalState GlobalState;

void ASIC_send_work(GlobalState *state, const asic_job_t *job);

#endif
