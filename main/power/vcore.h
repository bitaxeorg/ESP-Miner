#ifndef VCORE_H_
#define VCORE_H_

#include "global_state.h"

esp_err_t VCORE_init(BoardConfig * BOARD_CONFIG);
esp_err_t VCORE_set_voltage(BoardConfig * BOARD_CONFIG, float core_voltage);
int16_t VCORE_get_voltage_mv(BoardConfig * BOARD_CONFIG);
esp_err_t VCORE_check_fault(GlobalState * GLOBAL_STATE);
const char* VCORE_get_fault_string(BoardConfig * BOARD_CONFIG);

#endif /* VCORE_H_ */
