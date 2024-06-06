#include <stdio.h>
#include "esp_log.h"

#include "INA260.h"

// static const char *TAG = "INA260.c";

bool INA260_installed(void)
{
    uint8_t data[2];
    return i2c_master_register_read(INA260_I2CADDR_DEFAULT, INA260_REG_BUSVOLTAGE, data, 2) == ESP_OK;
}

float INA260_read_current(void)
{
    uint8_t data[2];

    ESP_ERROR_CHECK(i2c_master_register_read(INA260_I2CADDR_DEFAULT, INA260_REG_CURRENT, data, 2));
    // ESP_LOGI(TAG, "Raw Current = %02X %02X", data[1], data[0]);

    return (uint16_t)(data[1] | (data[0] << 8)) * 1.25;
}

float INA260_read_voltage(void)
{
    uint8_t data[2];

    ESP_ERROR_CHECK(i2c_master_register_read(INA260_I2CADDR_DEFAULT, INA260_REG_BUSVOLTAGE, data, 2));
    // ESP_LOGI(TAG, "Raw Voltage = %02X %02X", data[1], data[0]);

    return (uint16_t)(data[1] | (data[0] << 8)) * 1.25;
}

float INA260_read_power(void)
{
    uint8_t data[2];

    ESP_ERROR_CHECK(i2c_master_register_read(INA260_I2CADDR_DEFAULT, INA260_REG_POWER, data, 2));
    // ESP_LOGI(TAG, "Raw Power = %02X %02X", data[1], data[0]);

    return (data[1] | (data[0] << 8)) * 10;
}
