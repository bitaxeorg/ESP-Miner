#include <stdio.h>
#include "esp_log.h"

#include "i2c_bitaxe.h"
#include "INA260.h"

static const char *TAG = "INA260";

static i2c_master_dev_handle_t ina260_dev_handle;

/**
 * @brief Initialize the INA260 sensor.
 *
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t INA260_init(void)
{
    return i2c_bitaxe_add_device(INA260_I2CADDR_DEFAULT, &ina260_dev_handle, TAG);
}

float INA260_read_current(void)
{
    uint8_t data[2];

    ESP_ERROR_CHECK(i2c_bitaxe_register_read(ina260_dev_handle, INA260_REG_CURRENT, data, 2));
    // ESP_LOGI(TAG, "Raw Current = %02X %02X", data[1], data[0]);

    return (uint16_t)(data[1] | (data[0] << 8)) * 1.25;
}

float INA260_read_voltage(void)
{
    uint8_t data[2];

    ESP_ERROR_CHECK(i2c_bitaxe_register_read(ina260_dev_handle, INA260_REG_BUSVOLTAGE, data, 2));
    // ESP_LOGI(TAG, "Raw Voltage = %02X %02X", data[1], data[0]);

    return (uint16_t)(data[1] | (data[0] << 8)) * 1.25;
}

float INA260_read_power(void)
{
    uint8_t data[2];

    ESP_ERROR_CHECK(i2c_bitaxe_register_read(ina260_dev_handle, INA260_REG_POWER, data, 2));
    // ESP_LOGI(TAG, "Raw Power = %02X %02X", data[1], data[0]);

    return (data[1] | (data[0] << 8)) * 10;
}
