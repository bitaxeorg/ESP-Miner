#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c.h"

#include "INA260.h"

#define I2C_MASTER_SCL_IO 48        /*!< GPIO number used for I2C master clock */
#define I2C_MASTER_SDA_IO 47        /*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM 0            /*!< I2C master i2c port number, the number of i2c peripheral interfaces available will depend on the chip */
#define I2C_MASTER_FREQ_HZ 400000   /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS 1000

// static const char *TAG = "INA260.c";

/**
 * @brief Read a sequence of I2C bytes
 */
static esp_err_t register_read(uint8_t reg_addr, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(I2C_MASTER_NUM, INA260_I2CADDR_DEFAULT, &reg_addr, 1, data, len, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

/**
 * @brief Write a byte to a I2C register
 */
// static esp_err_t register_write_byte(uint8_t reg_addr, uint8_t data) {
//     int ret;
//     uint8_t write_buf[2] = {reg_addr, data};

//     ret = i2c_master_write_to_device(I2C_MASTER_NUM, INA260_I2CADDR_DEFAULT, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);

//     return ret;
// }
bool INA260_installed(void)
{
    uint8_t data[2];
    return register_read(INA260_REG_BUSVOLTAGE, data, 2) == ESP_OK;
}

float INA260_read_current(void)
{
    uint8_t data[2];

    ESP_ERROR_CHECK(register_read(INA260_REG_CURRENT, data, 2));
    // ESP_LOGI(TAG, "Raw Current = %02X %02X", data[1], data[0]);

    return (uint16_t)(data[1] | (data[0] << 8)) * 1.25;
}

float INA260_read_voltage(void)
{
    uint8_t data[2];

    ESP_ERROR_CHECK(register_read(INA260_REG_BUSVOLTAGE, data, 2));
    // ESP_LOGI(TAG, "Raw Voltage = %02X %02X", data[1], data[0]);

    return (uint16_t)(data[1] | (data[0] << 8)) * 1.25;
}

float INA260_read_power(void)
{
    uint8_t data[2];

    ESP_ERROR_CHECK(register_read(INA260_REG_POWER, data, 2));
    // ESP_LOGI(TAG, "Raw Power = %02X %02X", data[1], data[0]);

    return (data[1] | (data[0] << 8)) * 10;
}