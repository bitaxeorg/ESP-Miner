#ifndef WATCHDOGS_H_
#define WATCHDOGS_H_

#include "mining_watchdog.h"

typedef struct GlobalState GlobalState;

// Every result frame from the chain, from the ASIC result task
void WATCHDOGS_note_asic_result(void);

// Once from the power management task before its loop
void WATCHDOGS_init(void);

// From the power management loop. Handles pool stalls itself; returns what to
// do with the chain, which the power management task owns.
wd_asic_action_t WATCHDOGS_tick(GlobalState * GLOBAL_STATE);

// Called right before an esp_restart() the ASIC watchdog asked for
void WATCHDOGS_before_restart(void);

#endif /* WATCHDOGS_H_ */
