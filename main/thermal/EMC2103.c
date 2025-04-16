#include <stdio.h>
#include "esp_log.h"

#include "i2c_bitaxe.h"
#include "EMC2103.h"

static const char * TAG = "EMC2103";

static i2c_master_dev_handle_t EMC2103_dev_handle;

/**
 * @brief Initialize the EMC2103 sensor.
 *
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t EMC2103_init() {

    if (i2c_bitaxe_add_device(EMC2103_I2CADDR_DEFAULT, &EMC2103_dev_handle, TAG) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "EMC2103 init");

    // Configure the fan setting
    ESP_ERROR_CHECK(i2c_bitaxe_register_write_byte(EMC2103_dev_handle, EMC2103_CONFIGURATION1, 0));

    // //if (invertPolarity) {
    ESP_ERROR_CHECK(i2c_bitaxe_register_write_byte(EMC2103_dev_handle, EMC2103_PWM_CONFIG, 0x01));
    // //}

    return ESP_OK;

}

void EMC2103_set_ideality_factor(uint8_t ideality){
    //set Ideality Factor
    ESP_ERROR_CHECK(i2c_bitaxe_register_write_byte(EMC2103_dev_handle, EMC2103_EXTERNAL_DIODE1_IDEALITY, ideality));
    ESP_ERROR_CHECK(i2c_bitaxe_register_write_byte(EMC2103_dev_handle, EMC2103_EXTERNAL_DIODE2_IDEALITY, ideality));
}

void EMC2103_set_beta_compensation(uint8_t beta){
    //set Beta Compensation
    ESP_ERROR_CHECK(i2c_bitaxe_register_write_byte(EMC2103_dev_handle, EMC2103_EXTERNAL_DIODE1_BETA, beta));
    ESP_ERROR_CHECK(i2c_bitaxe_register_write_byte(EMC2103_dev_handle, EMC2103_EXTERNAL_DIODE2_BETA, beta));

}

/**
 * @brief Set the fan speed as a percentage.
 *
 * @param percent The desired fan speed as a percentage (0.0 to 1.0).
 */
void EMC2103_set_fan_speed(float percent)
{
    uint8_t setting = (uint8_t) (255.0 * percent);
    //ESP_LOGI(TAG, "Setting fan speed to %.2f%% (%d)", percent*100.0, setting);
    ESP_ERROR_CHECK(i2c_bitaxe_register_write_byte(EMC2103_dev_handle, EMC2103_FAN_SETTING, setting));
}

/**
 * @brief Get the current fan speed in RPM.
 *
 * @return uint16_t The fan speed in RPM.
 */
uint16_t EMC2103_get_fan_speed(void)
{
    uint8_t tach_lsb, tach_msb;
    uint16_t reading;
    uint32_t RPM;

    ESP_ERROR_CHECK(i2c_bitaxe_register_read(EMC2103_dev_handle, EMC2103_TACH_LSB, &tach_lsb, 1));
    ESP_ERROR_CHECK(i2c_bitaxe_register_read(EMC2103_dev_handle, EMC2103_TACH_MSB, &tach_msb, 1));

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

/**
 * @brief Get the external temperature in Celsius.
 *
 * @return float The external temperature in Celsius.
 */
float EMC2103_get_external_temp(void)
{
    uint8_t temp_msb, temp_lsb;
    uint16_t reading;

    float temp1, temp2;

    ESP_ERROR_CHECK(i2c_bitaxe_register_read(EMC2103_dev_handle, EMC2103_EXTERNAL_TEMP1_MSB, &temp_msb, 1));
    ESP_ERROR_CHECK(i2c_bitaxe_register_read(EMC2103_dev_handle, EMC2103_EXTERNAL_TEMP1_LSB, &temp_lsb, 1));

    //print the temps
    //ESP_LOGI(TAG, "Temp1 MSB: %02X Temp1 LSB: %02X", temp_msb, temp_lsb);
    
    // Combine MSB and LSB, and then right shift to get 11 bits
    reading = (temp_msb << 8) | temp_lsb;

    if (reading == EMC2103_TEMP_DIODE_FAULT) {
        ESP_LOGE(TAG, "EMC2103 TEMP_DIODE1_FAULT: %04X", reading);
    }

    reading >>= 5;  // Now, `reading` contains an 11-bit signed value

    // Cast `reading` to a signed 16-bit integer
    int16_t signed_reading = (int16_t)reading;

    // If the 11th bit (sign bit in 11-bit data) is set, extend the sign
    if (signed_reading & 0x0400) {
        signed_reading |= 0xF800;  // Set upper bits to extend the sign
    }

    // Convert the signed reading to temperature in Celsius
    temp1 = (float)signed_reading / 8.0;

    ESP_ERROR_CHECK(i2c_bitaxe_register_read(EMC2103_dev_handle, EMC2103_EXTERNAL_TEMP2_MSB, &temp_msb, 1));
    ESP_ERROR_CHECK(i2c_bitaxe_register_read(EMC2103_dev_handle, EMC2103_EXTERNAL_TEMP2_LSB, &temp_lsb, 1));

    //print the temps
    //ESP_LOGI(TAG, "Temp2 MSB: %02X Temp2 LSB: %02X", temp_msb, temp_lsb);
    
    // Combine MSB and LSB, and then right shift to get 11 bits
    reading = (temp_msb << 8) | temp_lsb;
    if (reading == EMC2103_TEMP_DIODE_FAULT) {
        ESP_LOGE(TAG, "EMC2103 TEMP_DIODE2_FAULT: %04X", reading);
    }
    reading >>= 5;  // Now, `reading` contains an 11-bit signed value

    // Cast `reading` to a signed 16-bit integer
    signed_reading = (int16_t)reading;

    // If the 11th bit (sign bit in 11-bit data) is set, extend the sign
    if (signed_reading & 0x0400) {
        signed_reading |= 0xF800;  // Set upper bits to extend the sign
    }

    // Convert the signed reading to temperature in Celsius
    temp2 = (float)signed_reading / 8.0;


    //debug the temps
    //ESP_LOGI(TAG, "Temp1: %.2f Temp2: %.2f", temp1, temp2);

    return temp1;
}
