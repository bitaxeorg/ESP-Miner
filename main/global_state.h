#ifndef GLOBAL_STATE_H_
#define GLOBAL_STATE_H_

#include "common.h"
#include "device_config.h"
#include "display.h"
#include "serial.h"
#include "statistics_task.h"
#include "stratum_api.h"
#include "work_queue.h"
#include <stdbool.h>
#include <stdint.h>

#define STRATUM_USER CONFIG_STRATUM_USER
#define FALLBACK_STRATUM_USER CONFIG_FALLBACK_STRATUM_USER

#define HISTORY_LENGTH 100
#define DIFF_STRING_SIZE 10


typedef struct
{

    


} GlobalState;

extern GlobalState GLOBAL_STATE;



#endif /* GLOBAL_STATE_H_ */
