#ifndef LVGLDISPLAY_H
#define LVGLDISPLAY_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"

#include "nvs_config.h"
#include "i2c_bitaxe.h"

esp_err_t lvglDisplay_init(void);
esp_err_t lvglUpdateDisplayValues(GlobalState *GLOBAL_STATE);

#endif