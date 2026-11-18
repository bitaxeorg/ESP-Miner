#ifndef POWER_SETTINGS_H
#define POWER_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct GlobalState GlobalState;

bool POWER_MANAGEMENT_apply_settings(GlobalState *state, uint16_t voltage_mv, float frequency,
                                     uint16_t *last_voltage_mv, float *last_frequency);

#endif
