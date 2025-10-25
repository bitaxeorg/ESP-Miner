#include <stdio.h>
#include "esp_log.h"
#include "esp_check.h"

#include "i2c_bitaxe.h"
#include "EMC2103.h"

static const char * TAG = "EMC2103";

static i2c_master_dev_handle_t EMC2103_dev_handle;

static int temp_offset;

/**
 * @brief Initialize the EMC2103 sensor.
 *
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t EMC2103_init(int temp_offset_param)
{
    ESP_LOGI(TAG, "Initializing EMC2103 (Temperature offset: %d° C)", temp_offset_param);
    
    temp_offset = temp_offset_param;

    if (i2c_bitaxe_add_device(EMC2103_I2CADDR_DEFAULT, &EMC2103_dev_handle, TAG) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "EMC2103 init");

    // Configure the fan setting
    ESP_RETURN_ON_ERROR(i2c_bitaxe_register_write_byte(EMC2103_dev_handle, EMC2103_CONFIGURATION1, 0), TAG, "Failed to configure EMC2103");
    ESP_RETURN_ON_ERROR(i2c_bitaxe_register_write_byte(EMC2103_dev_handle, EMC2103_PWM_CONFIG, 0x00), TAG, "Failed to configure PWM");

    return ESP_OK;

}

esp_err_t EMC2103_set_ideality_factor(uint8_t ideality){
    //set Ideality Factor
    ESP_RETURN_ON_ERROR(i2c_bitaxe_register_write_byte(EMC2103_dev_handle, EMC2103_EXTERNAL_DIODE1_IDEALITY, ideality), TAG, "Failed to set diode 1 ideality factor");
    ESP_RETURN_ON_ERROR(i2c_bitaxe_register_write_byte(EMC2103_dev_handle, EMC2103_EXTERNAL_DIODE2_IDEALITY, ideality), TAG, "Failed to set diode 2 ideality factor");
    return ESP_OK;
}

esp_err_t EMC2103_set_beta_compensation(uint8_t beta){
    //set Beta Compensation
    ESP_RETURN_ON_ERROR(i2c_bitaxe_register_write_byte(EMC2103_dev_handle, EMC2103_EXTERNAL_DIODE1_BETA, beta), TAG, "Failed to set diode 1 beta compensation");
    ESP_RETURN_ON_ERROR(i2c_bitaxe_register_write_byte(EMC2103_dev_handle, EMC2103_EXTERNAL_DIODE2_BETA, beta), TAG, "Failed to set diode 2 beta compensation");
    return ESP_OK;
}

/**
 * @brief Set the fan speed as a percentage.
 *
 * @param percent The desired fan speed as a percentage (0.0 to 1.0).
 */
esp_err_t EMC2103_set_fan_speed(float percent)
{
    uint8_t setting = (uint8_t) (255.0 * percent);
    //ESP_LOGI(TAG, "Setting fan speed to %.2f%% (%d)", percent*100.0, setting);
    ESP_RETURN_ON_ERROR(i2c_bitaxe_register_write_byte(EMC2103_dev_handle, EMC2103_FAN_SETTING, setting), TAG, "Failed to set fan speed");
    return ESP_OK;
}

/**
 * @brief Get the current fan speed in RPM.
 *
 * @return uint16_t The fan speed in RPM.
 */
uint16_t EMC2103_get_fan_speed(void)
{
    uint8_t tach_lsb = 0, tach_msb = 0;
    uint16_t reading;
    uint32_t RPM;
    esp_err_t err;

    err = i2c_bitaxe_register_read(EMC2103_dev_handle, EMC2103_TACH_LSB, &tach_lsb, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read fan speed LSB: %s", esp_err_to_name(err));
        return 0;
    }
    
    err = i2c_bitaxe_register_read(EMC2103_dev_handle, EMC2103_TACH_MSB, &tach_msb, 1);
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

static float get_external_temp(int i, uint8_t msb_register, uint8_t lsb_register)
{
    uint8_t temp_msb = 0, temp_lsb = 0;
    esp_err_t err;

    // Read temperature 1
    err = i2c_bitaxe_register_read(EMC2103_dev_handle, msb_register, &temp_msb, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read external temperature %d MSB: %s", i, esp_err_to_name(err));
        return -1;
    }
    
    err = i2c_bitaxe_register_read(EMC2103_dev_handle, lsb_register, &temp_lsb, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read external temperature %d LSB: %s", i, esp_err_to_name(err));
        return -1;
    }

    // Combine MSB and LSB, and then right shift to get 11 bits
    uint16_t reading = (temp_msb << 8) | temp_lsb;

    if (reading == EMC2103_TEMP_DIODE_FAULT) {
        ESP_LOGE(TAG, "EMC2103 TEMP_DIODE%d_FAULT: %04X", i, reading);
    }

    reading >>= 5;  // Now, `reading` contains an 11-bit signed value

    // Cast `reading` to a signed 16-bit integer
    int16_t signed_reading = (int16_t)reading;

    // If the 11th bit (sign bit in 11-bit data) is set, extend the sign
    if (signed_reading & 0x0400) {
        signed_reading |= 0xF800;  // Set upper bits to extend the sign
    }

    // Convert the signed reading to temperature in Celsius
    float result = (float)signed_reading / 8.0f;

    return result + temp_offset;
}

/**
 * @brief Get the external temperature in Celsius.
 *
 * @return float The external temperature in Celsius.
 */
float EMC2103_get_external_temp(void)
{
    return get_external_temp(1, EMC2103_EXTERNAL_TEMP1_MSB, EMC2103_EXTERNAL_TEMP1_LSB);
}

/**
 * @brief Get the external temperature 2 in Celsius.
 *
 * @return float The external temperature 2 in Celsius.
 */
float EMC2103_get_external_temp2(void)
{
    return get_external_temp(2, EMC2103_EXTERNAL_TEMP2_MSB, EMC2103_EXTERNAL_TEMP2_LSB);
}
