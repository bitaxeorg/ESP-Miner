#ifndef BZM_BOARD_POWER_H
#define BZM_BOARD_POWER_H
#include "esp_err.h"
#include "bonanza_power_policy.h"
typedef struct GlobalState GlobalState;
/* Hardware adapter. Lifecycle operations run only in BONANZA_POWER_MANAGEMENT_task. */
esp_err_t BZM_board_init(GlobalState *state);
bonanza_power_start_result_t BZM_board_start(void *context);
bool BZM_board_stop(void *context);
bool BZM_board_apply(void *context, bonanza_power_target_t target);
bool BZM_board_maintenance(void *context, bonanza_power_owner_t owner, bool acquire);
bonanza_power_sample_t BZM_board_sample(void);
#endif
