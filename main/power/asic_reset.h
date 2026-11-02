#ifndef ASIC_RESET_H_
#define ASIC_RESET_H_

#include <stdint.h>
#include "esp_err.h"

esp_err_t asic_reset(uint32_t low_ms, uint32_t release_ms);
esp_err_t asic_hold_reset_low(void);

#endif /* ASIC_RESET_H_ */
