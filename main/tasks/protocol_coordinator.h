#ifndef PROTOCOL_COORDINATOR_H_
#define PROTOCOL_COORDINATOR_H_

#include "global_state.h"

// Initialize the coordinator (call once from main before starting the task)
void protocol_coordinator_init(GlobalState *gs);

// Main coordinator task — manages protocol lifecycle and fallback
void protocol_coordinator_task(void *pvParameters);

// Called by protocol tasks to signal connection failure
void protocol_coordinator_notify_failure(void);

// V1 task checks this to know when to shut down gracefully
bool protocol_coordinator_v1_should_shutdown(void);

// V1 task calls this right before deleting itself
void protocol_coordinator_v1_exited(void);

// V2 task checks this to know when to shut down gracefully
bool protocol_coordinator_v2_should_shutdown(void);

// V2 task calls this right before deleting itself
void protocol_coordinator_v2_exited(void);

#endif // PROTOCOL_COORDINATOR_H_
