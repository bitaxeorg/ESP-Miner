#ifndef VCORE_H_
#define VCORE_H_

#include "global_state.h"

void VCORE_init(GlobalState * global_state);
bool VCORE_set_voltage(float core_voltage, GlobalState * global_state);
uint16_t VCORE_get_voltage_mv(GlobalState * global_state);

#endif /* VCORE_H_ */
