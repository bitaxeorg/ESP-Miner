#ifndef GATEWAY_TASK_H_
#define GATEWAY_TASK_H_

#include <stdbool.h>
#include <stdint.h>

// All shared gateway types (PeerMinerState, GatewayModule, MinerType, etc.)
// are now defined in the portable gateway component.
#include "gateway_types.h"

// No gateway_task — ws_client_task handles everything now.
// gateway_init() is called from main.c with a void* cast.
void gateway_init(void *global_state);

#endif /* GATEWAY_TASK_H_ */
