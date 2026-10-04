#ifndef BONANZA_POWER_MANAGEMENT_TASK_H_
#define BONANZA_POWER_MANAGEMENT_TASK_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "bonanza_power_policy.h"

typedef struct GlobalState GlobalState;

esp_err_t BONANZA_POWER_MANAGEMENT_init(GlobalState *state);
void BONANZA_POWER_MANAGEMENT_set_ready(void);
bool BONANZA_POWER_MANAGEMENT_wait_started(uint32_t timeout_ms);
bool BONANZA_POWER_MANAGEMENT_pause(void);
bool BONANZA_POWER_MANAGEMENT_resume(void);
bool BONANZA_POWER_MANAGEMENT_acquire_maintenance(bonanza_power_owner_t owner);
bool BONANZA_POWER_MANAGEMENT_release_maintenance(bonanza_power_owner_t owner);
bool BONANZA_POWER_MANAGEMENT_prepare_restart(void);
bool BONANZA_POWER_MANAGEMENT_stop_requested(void);
bool BONANZA_POWER_MANAGEMENT_fan_control_allowed(void);
/* Serialize fan/query I/O with lifecycle operations and exclude maintenance.
 * This lock does not authorize callers to change board power or clocks. */
bool BONANZA_POWER_MANAGEMENT_board_io_begin(void);
void BONANZA_POWER_MANAGEMENT_board_io_end(void);

#endif
