#ifndef VCORE_H_
#define VCORE_H_

#include "global_state.h"

esp_err_t VCORE_init(GlobalState * GLOBAL_STATE);
esp_err_t VCORE_set_voltage(GlobalState * GLOBAL_STATE, float core_voltage);
int16_t VCORE_get_voltage_mv(GlobalState * GLOBAL_STATE);
esp_err_t VCORE_check_fault(GlobalState * GLOBAL_STATE);
const char* VCORE_get_fault_string(GlobalState * GLOBAL_STATE);

#endif /* VCORE_H_ */
