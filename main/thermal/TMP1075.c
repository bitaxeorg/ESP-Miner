#include <stdio.h>
#include "esp_log.h"
#include "i2c_bitaxe.h"

#include "TMP1075.h"

static const char *TAG = "TMP1075";

static i2c_master_dev_handle_t tmp1075_dev_handle[2];

static int temp_offset;

/**
 * @brief Initialize the TMP1075 sensor.
 *
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t TMP1075_init(int temp_offset_param)
{
    ESP_ERROR_CHECK(i2c_bitaxe_add_device(TMP1075_I2CADDR_DEFAULT    , &tmp1075_dev_handle[0], TAG));
    ESP_ERROR_CHECK(i2c_bitaxe_add_device(TMP1075_I2CADDR_DEFAULT + 1, &tmp1075_dev_handle[1], TAG));

    temp_offset = temp_offset_param;

    return ESP_OK;
}

float TMP1075_read_temperature(int device_index)
{
    if (device_index < 0 || device_index >= 2) {
        ESP_LOGE(TAG, "Invalid device index");
        return -1;
    }
    
    uint8_t data[2];
    esp_err_t err = i2c_bitaxe_register_read(tmp1075_dev_handle[device_index], TMP1075_TEMP_REG, data, 2);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read temperature from device index %d: %s", device_index, esp_err_to_name(err));
        return -1;
    }

    int16_t raw_temp = ((int16_t)data[0] << 8) | data[1];
    raw_temp = raw_temp >> 4;
    float result = raw_temp * 0.0625f;
    return result + temp_offset;
}
