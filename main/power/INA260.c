#include <stdio.h>
#include "esp_log.h"

#include "i2c_bitaxe.h"
#include "INA260.h"

static const char *TAG = "INA260";

static i2c_master_dev_handle_t ina260_dev_handle;

// Cached values to handle I2C failures robustly
static float last_current = 0.0f;
static float last_voltage = 0.0f;
static float last_power   = 0.0f;

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

    if (i2c_bitaxe_register_read(ina260_dev_handle, INA260_REG_CURRENT, data, 2) != ESP_OK) {
        ESP_LOGE(TAG, "Could not read current");
        return last_current;
    }
    
    last_current = (uint16_t)(data[1] | (data[0] << 8)) * 1.25;
    return last_current;
}

float INA260_read_voltage(void)
{
    uint8_t data[2];

    if (i2c_bitaxe_register_read(ina260_dev_handle, INA260_REG_BUSVOLTAGE, data, 2) != ESP_OK) {
        ESP_LOGE(TAG, "Could not read voltage");
        return last_voltage;
    }
    
    last_voltage = (uint16_t)(data[1] | (data[0] << 8)) * 1.25;
    return last_voltage;
}

float INA260_read_power(void)
{
    uint8_t data[2];

    if (i2c_bitaxe_register_read(ina260_dev_handle, INA260_REG_POWER, data, 2) != ESP_OK) {
        ESP_LOGE(TAG, "Could not read power");
        return last_power;
    }
    
    last_power = (data[1] | (data[0] << 8)) * 10;
    return last_power;
}
