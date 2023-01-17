#ifndef DS4432U_H_
#define DS4432U_H_

#include "driver/i2c.h"

esp_err_t i2c_master_init(void);
esp_err_t i2c_master_delete(void);
void DS4432U_read(void);
void DS4432U_set(uint8_t);
uint8_t voltage_to_reg(float vout);

#endif /* DS4432U_H_ */