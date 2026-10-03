#ifndef ASIC_INTERNAL_H_
#define ASIC_INTERNAL_H_

#include "asic_job.h"

typedef struct GlobalState GlobalState;

/* Takes ownership of the common job and dispatches it to the selected driver. */
void ASIC_send_work(GlobalState *state, asic_job_t *job);

#endif
