#ifndef I2C_MASTER_H_
#define I2C_MASTER_H_

#include "driver/i2c.h"

#define I2C_MASTER_NUM 0            /*!< I2C master i2c port number, the number of i2c peripheral interfaces available will depend on the chip */
#define I2C_MASTER_TIMEOUT_MS 1000

esp_err_t i2c_master_init(void);
esp_err_t i2c_master_delete(void);
esp_err_t i2c_master_register_read(uint8_t device_address, uint8_t reg_addr, uint8_t * data, size_t len);
esp_err_t i2c_master_register_write_byte(uint8_t device_address, uint8_t reg_addr, uint8_t data);
esp_err_t i2c_master_register_write_word(uint8_t device_address, uint8_t reg_addr, uint16_t data);

#endif /* I2C_MASTER_H_ */
