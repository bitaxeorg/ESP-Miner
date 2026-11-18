#include <stdio.h>
#include "esp_log.h"
#include "esp_check.h"

#include "i2c_bitaxe.h"
#include "EMC2101.h"

static const char * TAG = "EMC2101";

static i2c_master_dev_handle_t emc2101_dev_handle;

static int temp_offset;
static emc2101_chip_type_t chip_type = CHIP_EMC2101;

/**
 * @brief Initialize the EMC2101 sensor.
 *
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t EMC2101_init(int temp_offset_param)
{
    ESP_LOGI(TAG, "Initializing EMC2101 (Temperature offset: %d° C)", temp_offset_param);

    temp_offset = temp_offset_param;

    if (i2c_bitaxe_add_device(EMC2101_I2CADDR_DEFAULT, &emc2101_dev_handle, TAG) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device");
        return ESP_FAIL;
    }

    // Detect EMC2101 vs CTF2301B clone (same address, different config registers).
    uint8_t chip_id = 0, mfg_id = 0;
    uint8_t ids_ok = i2c_bitaxe_register_read(emc2101_dev_handle, CTF2301B_REG_CHIP_ID, &chip_id, 1) == ESP_OK;
    ids_ok = i2c_bitaxe_register_read(emc2101_dev_handle, CTF2301B_REG_MFG_ID, &mfg_id, 1) == ESP_OK && ids_ok;
    chip_type = (ids_ok && chip_id == CTF2301B_CHIP_ID && mfg_id == CTF2301B_MFG_ID) ? CHIP_CTF2301B : CHIP_EMC2101;
    ESP_LOGI(TAG, "Detected %s (chip id 0x%02X, mfg id 0x%02X)",
             chip_type == CHIP_CTF2301B ? "CTF2301B" : "EMC2101", chip_id, mfg_id);

    // set the TACH input
    ESP_RETURN_ON_ERROR(i2c_bitaxe_register_write_byte(emc2101_dev_handle, EMC2101_REG_CONFIG, 0x04), TAG, "Failed to set TACH input");


    //! deprecated bit was never set successfully
    // --- Configure Fan Settings (Register 0x4A) ---
    // Start with a base configuration:
    // Bit 7 (MASK) = 0: Tach Alert Enabled
    // Bit 6 (EN)   = 1: *** FAN DRIVER ENABLED ***
    // Bit 5 (LEVEL)= 1: Direct Setting Mode (use PWM Duty Cycle Reg 0x4C)
    // Bit 4 (POL)  = 0: Normal Polarity (placeholder, set below)
    // Bit 3:2(EDGES)=00: 2 Tach pulses per revolution (standard)
    // Bit 1:0(RANGE)=00: PWM Frequency ~22.5 kHz (common setting)
    // //if (invertPolarity) {
        ESP_RETURN_ON_ERROR(i2c_bitaxe_register_write_byte(emc2101_dev_handle, EMC2101_FAN_CONFIG, 0b00100011), TAG, "Failed to configure fan settings");
    // //}



    if (chip_type == CHIP_CTF2301B) {
        // Clone needs PHR set around the fan-range write and 0x21=0x02 for its PWM path.
        uint8_t reg_config = 0;
        ESP_RETURN_ON_ERROR(i2c_bitaxe_register_read(emc2101_dev_handle, EMC2101_REG_CONFIG, &reg_config, 1), TAG, "Failed to read config reg");
        ESP_RETURN_ON_ERROR(i2c_bitaxe_register_write_byte(emc2101_dev_handle, EMC2101_REG_CONFIG, reg_config | 0x02), TAG, "Failed to set PHR bit");
        ESP_RETURN_ON_ERROR(i2c_bitaxe_register_write_byte(emc2101_dev_handle, 0x19, 0x7F), TAG, "Failed to configure fan range");
        ESP_RETURN_ON_ERROR(i2c_bitaxe_register_write_byte(emc2101_dev_handle, EMC2101_REG_CONFIG, reg_config), TAG, "Failed to clear PHR bit");
        ESP_RETURN_ON_ERROR(i2c_bitaxe_register_write_byte(emc2101_dev_handle, 0x21, 0x02), TAG, "Failed to configure CTF2301B PWM");
    }


    // We're using default filtering and conversion, no need to set them again.
    // //set filtering
    // ESP_ERROR_CHECK(i2c_bitaxe_register_write_byte(emc2101_dev_handle, EMC2101_TEMP_FILTER, EMC2101_DEFAULT_FILTER));

    // //set conversion rate
    // ESP_ERROR_CHECK(i2c_bitaxe_register_write_byte(emc2101_dev_handle, EMC2101_REG_DATA_RATE, EMC2101_DEFAULT_DATARATE));

    return ESP_OK;
}

esp_err_t EMC2101_set_ideality_factor(uint8_t ideality){
    if (chip_type == CHIP_CTF2301B) {
        // CTF2301B NC table differs: 0x24 (eta 1.0319) -> 0x08 (eta 1.0356), closest match
        uint8_t nc_value = (ideality == 0x24) ? 0x08 : ideality;
        ESP_RETURN_ON_ERROR(i2c_bitaxe_register_write_byte(emc2101_dev_handle, CTF2301B_REG_NC, nc_value), TAG, "Failed to set NC factor");
    } else {
        //set Ideality Factor
        ESP_RETURN_ON_ERROR(i2c_bitaxe_register_write_byte(emc2101_dev_handle, EMC2101_IDEALITY_FACTOR, ideality), TAG, "Failed to set ideality factor");
    }
    return ESP_OK;
}

esp_err_t EMC2101_set_beta_compensation(uint8_t beta){
    if (chip_type == CHIP_CTF2301B) {
        // CTF2301B disables TruTCRIT compensation through register 0x30
        ESP_RETURN_ON_ERROR(i2c_bitaxe_register_write_byte(emc2101_dev_handle, CTF2301B_REG_BETA, beta), TAG, "Failed to set beta compensation for CTF2301B");
    } else {
        //set Beta Compensation
        ESP_RETURN_ON_ERROR(i2c_bitaxe_register_write_byte(emc2101_dev_handle, EMC2101_BETA_COMPENSATION, beta), TAG, "Failed to set beta compensation");
    }
    return ESP_OK;
}

// takes a fan speed percent
esp_err_t EMC2101_set_fan_speed(float percent)
{
    uint8_t speed;

    speed = (uint8_t) (63.0 * percent);
    ESP_RETURN_ON_ERROR(i2c_bitaxe_register_write_byte(emc2101_dev_handle, EMC2101_REG_FAN_SETTING, speed), TAG, "Failed to set fan speed");
    return ESP_OK;
}

// RPM = 5400000/reading
uint16_t EMC2101_get_fan_speed(void)
{
    uint8_t tach_lsb = 0, tach_msb = 0;
    uint16_t reading;
    uint16_t RPM;
    esp_err_t err;

    err = i2c_bitaxe_register_read(emc2101_dev_handle, EMC2101_TACH_LSB, &tach_lsb, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read fan speed LSB: %s", esp_err_to_name(err));
        return 0;
    }
    
    err = i2c_bitaxe_register_read(emc2101_dev_handle, EMC2101_TACH_MSB, &tach_msb, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read fan speed MSB: %s", esp_err_to_name(err));
        return 0;
    }

    // ESP_LOGI(TAG, "Raw Fan Speed = %02X %02X", tach_msb, tach_lsb);

    reading = tach_lsb | (tach_msb << 8);
    RPM = 5400000 / reading;

    // ESP_LOGI(TAG, "Fan Speed = %d RPM", RPM);
    if (RPM == 82) {
        return 0;
    }
    return RPM;
}

float EMC2101_get_external_temp(void)
{
    uint8_t temp_msb = 0, temp_lsb = 0;
    uint16_t reading;
    esp_err_t err;

    err = i2c_bitaxe_register_read(emc2101_dev_handle, EMC2101_EXTERNAL_TEMP_MSB, &temp_msb, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read external temperature MSB: %s", esp_err_to_name(err));
        return -1;
    }
    
    err = i2c_bitaxe_register_read(emc2101_dev_handle, EMC2101_EXTERNAL_TEMP_LSB, &temp_lsb, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read external temperature LSB: %s", esp_err_to_name(err));
        return -1;
    }
    
    // Combine MSB and LSB, and then right shift to get 11 bits
    reading = (temp_msb << 8) | temp_lsb;
    reading >>= 5;  // Now, `reading` contains an 11-bit signed value

    // Cast `reading` to a signed 16-bit integer
    int16_t signed_reading = (int16_t)reading;

    // If the 11th bit (sign bit in 11-bit data) is set, extend the sign
    if (signed_reading & 0x0400) {
        signed_reading |= 0xF800;  // Set upper bits to extend the sign
    }

    if (signed_reading == EMC2101_TEMP_FAULT_OPEN_CIRCUIT) {
        ESP_LOGE(TAG, "EMC2101 TEMP_FAULT_OPEN_CIRCUIT: %04X", signed_reading);
    }
    if (signed_reading == EMC2101_TEMP_FAULT_SHORT) {
        ESP_LOGE(TAG, "EMC2101 TEMP_FAULT_SHORT: %04X", signed_reading);
    }

    // Convert the signed reading to temperature in Celsius
    float result = (float)signed_reading / 8.0;

    return result + temp_offset;
}

float EMC2101_get_internal_temp(void)
{
    uint8_t temp = 0;
    esp_err_t err;
    
    err = i2c_bitaxe_register_read(emc2101_dev_handle, EMC2101_INTERNAL_TEMP, &temp, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read internal temperature: %s", esp_err_to_name(err));
        return -1;
    }
    return (float) temp + temp_offset;
}
