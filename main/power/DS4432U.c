#include <stdio.h>
#include <math.h>
#include "esp_log.h"

#include "i2c_bitaxe.h"
#include "DS4432U.h"

// DS4432U+ -- Adjustable current DAC
#define DS4432U_SENSOR_ADDR 0x48 // Slave address of the DS4432U+
#define DS4432U_OUT0_REG 0xF8    // register for current output 0
#define DS4432U_OUT1_REG 0xF9    // register for current output 1

// DS4432U Transfer function constants for Bitaxe board
// #define BITAXE_RFS 80000.0     // R16
// #define BITAXE_IFS ((DS4432_VRFS * 127.0) / (BITAXE_RFS * 16))
#define BITAXE_IFS 0.000098921 // (Vrfs / Rfs) x (127/16)  -> Vrfs = 0.997, Rfs = 80000
#define BITAXE_RA 4750.0       // R14
#define BITAXE_RB 3320.0       // R15
#define BITAXE_VNOM 1.451   // this is with the current DAC set to 0. Should be pretty close to (VFB*(RA+RB))/RB
#define BITAXE_VMAX 2.39
#define BITAXE_VMIN 0.046

#define TPS40305_VFB 0.6

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

esp_err_t DS4432U_set_voltage(float vout) {
    float change;
    uint8_t reg;

    // make sure the requested voltage is in within range of BITAXE_VMIN and BITAXE_VMAX
    if (vout > BITAXE_VMAX || vout < BITAXE_VMIN) {
        return ESP_FAIL;
    }

    // this is the transfer function. comes from the DS4432U+ datasheet
    change = fabs((((TPS40305_VFB / BITAXE_RB) - ((vout - TPS40305_VFB) / BITAXE_RA)) / BITAXE_IFS) * 127);
    reg = (uint8_t)ceil(change);

    // Set the MSB high if the requested voltage is BELOW nominal
    if (vout < BITAXE_VNOM) {
        reg |= 0x80;
    }

    ESP_RETURN_ON_ERROR(DS4432U_set_current_code(0, reg), TAG, "DS4432U set current code failed!");

    return ESP_OK;
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
