#ifndef POWER_H
#define POWER_H

#include <esp_err.h>
#include "global_state.h"

float Power_get_current(BoardConfig * BOARD_CONFIG);
float Power_get_power(BoardConfig * BOARD_CONFIG);
float Power_get_input_voltage(BoardConfig * BOARD_CONFIG);
float Power_get_vreg_temp(BoardConfig * BOARD_CONFIG);

#endif // POWER_H
