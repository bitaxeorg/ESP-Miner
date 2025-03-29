#pragma once

#include "influx.h"

void influx_task_set_temperature(float temp, float temp2);
void influx_task_set_pwr(float voltage, float current, float power);

void influx_task(void *pvParameters);