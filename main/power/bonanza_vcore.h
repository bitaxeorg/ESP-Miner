#ifndef BONANZA_VCORE_H
#define BONANZA_VCORE_H
#include "TPS546.h"
typedef struct GlobalState GlobalState;
esp_err_t BONANZA_VCORE_init(GlobalState *state, TPS546_CONFIG config);
/* Positive calls prepare 2.8 V before ASIC initialization; reapply afterward. Zero shuts down. */
esp_err_t BONANZA_VCORE_set_voltage(GlobalState *state, float volts);
#endif
