#ifndef BONANZA_VCORE_H
#define BONANZA_VCORE_H
#include "bonanza_tps546.h"
typedef struct GlobalState GlobalState;
esp_err_t BONANZA_VCORE_init(GlobalState *state);
esp_err_t BONANZA_VCORE_bzm_set_rail_enabled(GlobalState *state, bool enabled);
esp_err_t BONANZA_VCORE_bzm_set_runtime_voltage(GlobalState *state, float volts);
esp_err_t BONANZA_VCORE_bzm_force_regulator_off(GlobalState *state);
esp_err_t BONANZA_VCORE_bzm_snapshot(BONANZA_TPS546_StatusSnapshot *snapshot, bool *pgood);
#endif
