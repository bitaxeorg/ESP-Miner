#include <stdio.h>
#include "esp_log.h"
#include "esp_check.h"

#include "i2c_bitaxe.h"
#include "EMC2302.h"

static const char * TAG = "EMC2302";

static i2c_master_dev_handle_t EMC2302_dev_handle;

/**
 * @brief Initialize the EMC2302 sensor.
 *
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t EMC2302_init()
{
    if (i2c_bitaxe_add_device(EMC2302_I2CADDR_DEFAULT, &EMC2302_dev_handle, TAG) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "EMC2302 init");

    return ESP_OK;
}

/**
 * @brief Set the fan speed as a percentage.
 *
 * @param percent The desired fan speed as a percentage (0.0 to 1.0).
 */
esp_err_t EMC2302_set_fan_speed(float percent)
{
    uint8_t setting = (uint8_t) (255.0 * percent);
    ESP_RETURN_ON_ERROR(i2c_bitaxe_register_write_byte(EMC2302_dev_handle, EMC2302_FAN1_SETTING, setting), TAG, "Failed to set fan speed");
    ESP_RETURN_ON_ERROR(i2c_bitaxe_register_write_byte(EMC2302_dev_handle, EMC2302_FAN2_SETTING, setting), TAG, "Failed to set fan speed");
    return ESP_OK;
}

/**
 * @brief Get the current fan speed in RPM.
 *
 * @return uint16_t The fan speed in RPM.
 */
uint16_t EMC2302_get_fan_speed(void)
{
    uint8_t tach_lsb = 0, tach_msb = 0;
    uint16_t reading;
    uint32_t RPM;
    esp_err_t err;

    err = i2c_bitaxe_register_read(EMC2302_dev_handle, EMC2302_TACH1_LSB, &tach_lsb, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read fan speed LSB: %s", esp_err_to_name(err));
        return 0;
    }
    
    err = i2c_bitaxe_register_read(EMC2302_dev_handle, EMC2302_TACH1_MSB, &tach_msb, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read fan speed MSB: %s", esp_err_to_name(err));
        return 0;
    }

    // ESP_LOGI(TAG, "Raw Fan Speed = %02X %02X", tach_msb, tach_lsb);

    reading = tach_lsb | (tach_msb << 8);
    reading >>= 3;

    //RPM = (3,932,160 * m)/reading
    //m is the multipler, which is default 2
    RPM = 7864320 / reading;

    // ESP_LOGI(TAG, "Fan Speed = %d RPM", RPM);
    if (RPM == 82) {
        return 0;
    }
    return RPM;
}
