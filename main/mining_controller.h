#ifndef MINING_CONTROLLER_H_
#define MINING_CONTROLLER_H_

#include "global_state.h"
#include "esp_err.h"

esp_err_t start_mining(GlobalState *global_state);
esp_err_t stop_mining(GlobalState *global_state);

#endif
