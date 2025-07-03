#ifndef THERMAL_H
#define THERMAL_H

#include <stdbool.h>
#include <esp_err.h>

#include "EMC2101.h"
#include "EMC2103.h"
#include "global_state.h"

esp_err_t Thermal_init(BoardConfig * BOARD_CONFIG);
esp_err_t Thermal_set_fan_percent(BoardConfig * BOARD_CONFIG, float percent);
uint16_t Thermal_get_fan_speed(BoardConfig * BOARD_CONFIG);

float Thermal_get_chip_temp(BoardConfig * BOARD_CONFIG);

#endif // THERMAL_H
