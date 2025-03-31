#ifndef VCORE_H_
#define VCORE_H_

#include "global_state.h"

esp_err_t VCORE_init(GlobalState * global_state);
esp_err_t VCORE_set_voltage(float core_voltage, GlobalState * global_state);
uint16_t VCORE_get_voltage_mv(GlobalState * global_state);

#endif /* VCORE_H_ */
