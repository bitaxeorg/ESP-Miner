#ifndef SETUP_BLE_H_
#define SETUP_BLE_H_

#include "esp_err.h"
#include "global_state.h"

esp_err_t setup_ble_start(GlobalState *global_state);
void setup_ble_stop(void);

#endif /* SETUP_BLE_H_ */
