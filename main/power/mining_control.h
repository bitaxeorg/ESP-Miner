#ifndef MINING_CONTROL_H_
#define MINING_CONTROL_H_

#include "global_state.h"
#include <stdint.h>

/**
 * Stop mining: cuts ASIC power, holds reset, marks ASIC uninitialized,
 * waits for tasks to drain in-flight UART operations, then flushes UART.
 */
void mining_stop(GlobalState * GLOBAL_STATE);

/**
 * Start mining: restores voltage from NVS, waits for stabilization,
 * flushes UART, then reinitializes ASIC via recovery sequence.
 *
 * @return number of chips detected (0 on failure)
 */
uint8_t mining_start(GlobalState * GLOBAL_STATE);

#endif /* MINING_CONTROL_H_ */
