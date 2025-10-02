#include <stdio.h>
#include "esp_log.h"
#include "i2c_bitaxe.h"

#include "TMP1075.h"

static const char *TAG = "TMP1075";

static i2c_master_dev_handle_t tmp1075_dev1_handle;
static i2c_master_dev_handle_t tmp1075_dev2_handle;

/**
 * @brief Initialize the TMP1075 sensor.
 *
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t TMP1075_init(void)
{
    ESP_ERROR_CHECK(i2c_bitaxe_add_device(TMP1075_I2CADDR_DEFAULT    , &tmp1075_dev1_handle, TAG));
    ESP_ERROR_CHECK(i2c_bitaxe_add_device(TMP1075_I2CADDR_DEFAULT + 1, &tmp1075_dev2_handle, TAG));
    return ESP_OK;
}

float TMP1075_read_temperature(int device_index)
{
    uint8_t data[2];

    switch (device_index) {
        case 0: 
            ESP_ERROR_CHECK(i2c_bitaxe_register_read(tmp1075_dev1_handle, TMP1075_TEMP_REG, data, 2));
            break;
        case 1: 
            ESP_ERROR_CHECK(i2c_bitaxe_register_read(tmp1075_dev2_handle, TMP1075_TEMP_REG, data, 2));
            break;
        default:
            return 0;
    }

    int16_t raw_temp = ((int16_t)data[0] << 8) | data[1];
    raw_temp = raw_temp >> 4;
    return raw_temp * 0.0625f;
}
