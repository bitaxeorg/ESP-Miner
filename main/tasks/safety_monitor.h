#ifndef SAFETY_MONITOR_H_
#define SAFETY_MONITOR_H_

#include <stdint.h>

typedef struct GlobalState GlobalState;

// Called from the power management loop while mining. Flags a hardware fault,
// which stops mining and cuts core power, when a fan stalls, a temperature
// sensor stops answering, a chip stops hashing or the core voltage collapses.
void SAFETY_check(GlobalState * GLOBAL_STATE, uint16_t core_voltage_target_mv);

#endif /* SAFETY_MONITOR_H_ */
