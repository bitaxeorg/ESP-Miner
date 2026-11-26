#ifndef BZM_BRIDGE_RESET_H
#define BZM_BRIDGE_RESET_H

#include "bzm/control.h"

esp_err_t bzm_bridge_reset(gpio_num_t clock, gpio_num_t data);

#endif
