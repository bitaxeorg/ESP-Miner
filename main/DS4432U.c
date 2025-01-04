#include <stdio.h>
#include <math.h>
#include "esp_log.h"

#include "i2c_bitaxe.h"
#include "DS4432U.h"

// DS4432U+ -- Adjustable current DAC
#define DS4432U_SENSOR_ADDR 0x48 // Slave address of the DS4432U+
#define DS4432U_OUT0_REG 0xF8    // register for current output 0
#define DS4432U_OUT1_REG 0xF9    // register for current output 1

static const char *TAG = "DS4432U";

static i2c_master_dev_handle_t ds4432u_dev_handle;

/**
 * @brief Initialize the DS4432U+ sensor.
 *
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t DS4432U_init(void) {
    return i2c_bitaxe_add_device(DS4432U_SENSOR_ADDR, &ds4432u_dev_handle, TAG);
}

/**
 * @brief Set the current DAC code for a specific DS4432U output.
 *
 * @param output The output channel (0 or 1).
 * @param code The current code value to set.
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t DS4432U_set_current_code(uint8_t output, uint8_t code) {
    uint8_t reg = (output == 0) ? DS4432U_OUT0_REG : DS4432U_OUT1_REG;
    return i2c_bitaxe_register_write_byte(ds4432u_dev_handle, reg, code);
}

/**
 * @brief Get the current DAC code value for a specific DS4432U output.
 *
 * @param output The output channel (0 or 1).
 * @param code Pointer to store the current code value.
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t DS4432U_get_current_code(uint8_t output, uint8_t *code) {
    uint8_t reg = (output == 0) ? DS4432U_OUT0_REG : DS4432U_OUT1_REG;
    return i2c_bitaxe_register_read(ds4432u_dev_handle, reg, code, 1);
}

esp_err_t DS4432U_test(void)
{
    uint8_t data;

    /* Read the DS4432U+ WHO_AM_I register, on power up the register should have the value 0x00 */
    ESP_RETURN_ON_ERROR(i2c_bitaxe_register_read(ds4432u_dev_handle, DS4432U_OUT0_REG, &data, 1), TAG, "Failed to read DS4432U+ OUT0 register");
    ESP_LOGI(TAG, "DS4432U+ OUT0 = 0x%02X", data);
    
    return ESP_OK;
}
