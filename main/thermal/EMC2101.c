#include <stdio.h>
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "i2c_bitaxe.h"
#include "EMC2101.h"

static const char * TAG = "EMC2101";

static i2c_master_dev_handle_t emc2101_dev_handle;

static int temp_offset;

// PWM_F (register 0x4D) sets the direct-mode duty ceiling: duty = FAN_SETTING / (2*PWM_F).
// Reset value is 0x17 (23), so the maximum meaningful FAN_SETTING is 46, not 63.
static uint8_t pwm_f = 0x17;

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

    // set the TACH input
    ESP_RETURN_ON_ERROR(i2c_bitaxe_register_write_byte(emc2101_dev_handle, EMC2101_REG_CONFIG, 0x04), TAG, "Failed to set TACH input");

    // PWM_F defines the direct-mode duty resolution. Read it instead of assuming 23,
    // so the percentage -> register scaling matches what the chip actually does.
    uint8_t pwm_f_read = 0;
    if (i2c_bitaxe_register_read(emc2101_dev_handle, EMC2101_PWM_FREQ, &pwm_f_read, 1) == ESP_OK && pwm_f_read > 0) {
        pwm_f = pwm_f_read;
    }
    ESP_LOGI(TAG, "PWM_F=0x%02X (%u) -> FAN_SETTING full scale %u", pwm_f, pwm_f, 2u * pwm_f);


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
    



    // We're using default filtering and conversion, no need to set them again.
    // //set filtering
    // ESP_ERROR_CHECK(i2c_bitaxe_register_write_byte(emc2101_dev_handle, EMC2101_TEMP_FILTER, EMC2101_DEFAULT_FILTER));

    // //set conversion rate
    // ESP_ERROR_CHECK(i2c_bitaxe_register_write_byte(emc2101_dev_handle, EMC2101_REG_DATA_RATE, EMC2101_DEFAULT_DATARATE));

    return ESP_OK;
}

esp_err_t EMC2101_set_ideality_factor(uint8_t ideality){
    //set Ideality Factor
    ESP_RETURN_ON_ERROR(i2c_bitaxe_register_write_byte(emc2101_dev_handle, EMC2101_IDEALITY_FACTOR, ideality), TAG, "Failed to set ideality factor");
    return ESP_OK;
}

esp_err_t EMC2101_set_beta_compensation(uint8_t beta){
    //set Beta Compensation
    ESP_RETURN_ON_ERROR(i2c_bitaxe_register_write_byte(emc2101_dev_handle, EMC2101_BETA_COMPENSATION, beta), TAG, "Failed to set beta compensation");
    return ESP_OK;
}

// takes a fan speed percent
// I2C reads on these boards occasionally return a transient error (bus contention with
// the ASIC UART activity, or a marginal pull-up). A single failed read used to surface as
// a 0 C temperature / 0 RPM, which the power manager interprets as a fault. Retry a few
// times before propagating a one-off glitch.
static esp_err_t emc_read_retry(uint8_t reg, uint8_t *buf, size_t len)
{
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < 3; attempt++) {
        err = i2c_bitaxe_register_read(emc2101_dev_handle, reg, buf, len);
        if (err == ESP_OK) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return err;
}

esp_err_t EMC2101_set_fan_speed(float percent)
{
    // Direct-mode duty cycle = FAN_SETTING / (2 * PWM_F). Scale against that ceiling,
    // not against a hardcoded 63: 63 overshoots a default PWM_F of 23 by ~1.37x and
    // makes everything above ~73% request saturate at 100% duty.
    if (percent < 0.0f) percent = 0.0f;
    if (percent > 1.0f) percent = 1.0f;

    float full_scale = 2.0f * (float) pwm_f;
    uint8_t speed = (uint8_t) (full_scale * percent + 0.5f);

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

    err = emc_read_retry(EMC2101_TACH_LSB, &tach_lsb, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read fan speed LSB: %s", esp_err_to_name(err));
        return 0;
    }
    
    err = emc_read_retry(EMC2101_TACH_MSB, &tach_msb, 1);
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

    err = emc_read_retry(EMC2101_EXTERNAL_TEMP_MSB, &temp_msb, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read external temperature MSB: %s", esp_err_to_name(err));
        return -1;
    }
    
    err = emc_read_retry(EMC2101_EXTERNAL_TEMP_LSB, &temp_lsb, 1);
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
    
    err = emc_read_retry(EMC2101_INTERNAL_TEMP, &temp, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read internal temperature: %s", esp_err_to_name(err));
        return -1;
    }
    return (float) temp + temp_offset;
}
