#ifndef VCORE_H_
#define VCORE_H_

#include "global_state.h"

esp_err_t VCORE_init();
esp_err_t VCORE_set_voltage(float core_voltage);
int16_t VCORE_get_voltage_mv();
esp_err_t VCORE_check_fault();
const char* VCORE_get_fault_string();

#endif /* VCORE_H_ */
