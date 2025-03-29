#ifndef DS4432U_H_
#define DS4432U_H_

#include <stdbool.h>
#include "esp_check.h"

#define DS4432_VRFS 0.997

esp_err_t DS4432U_test(void);
esp_err_t DS4432U_init(void);
esp_err_t DS4432U_set_current_code(uint8_t output, uint8_t code);
esp_err_t DS4432U_get_current_code(uint8_t output, uint8_t *code);
esp_err_t DS4432U_set_voltage(float vout);

#endif /* DS4432U_H_ */
