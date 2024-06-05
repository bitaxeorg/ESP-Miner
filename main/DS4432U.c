#include <stdio.h>
#include <math.h>
#include "esp_log.h"

#include "DS4432U.h"

// DS4432U+ -- Adjustable current DAC for use with the TPS40305 voltage regulator
// address: 0x90
#define DS4432U_SENSOR_ADDR 0x48 // Slave address of the DS4432U+
#define DS4432U_OUT0_REG 0xF8    // register for current output 0
#define DS4432U_OUT1_REG 0xF9    // register for current output 1

// DS4432U Transfer function constants
#define VFB 0.6
#define IFS 0.000098921 // (Vrfs / Rfs) x (127/16)  -> Vrfs = 0.997, Rfs = 80000
#define RA 4750.0       // R14
#define RB 3320.0       // R15
#define NOMINAL 1.451   // this is with the current DAC set to 0. Should be pretty close to (VFB*(RA+RB))/RB
#define MAXV 2.39
#define MINV 0.046

static const char *TAG = "DS4432U.c";

/**
 * @brief voltage_to_reg takes a voltage and returns a register setting for the DS4432U to get that voltage on the TPS40305
 * careful with this one!!
 */
static uint8_t voltage_to_reg(float vout)
{
    float change;
    uint8_t reg;

    // make sure the requested voltage is in within range of MINV and MAXV
    if (vout > MAXV || vout < MINV)
    {
        return 0;
    }

    // this is the transfer function. comes from the DS4432U+ datasheet
    change = fabs((((VFB / RB) - ((vout - VFB) / RA)) / IFS) * 127);
    reg = (uint8_t)ceil(change);

    // Set the MSB high if the requested voltage is BELOW nominal
    if (vout < NOMINAL)
    {
        reg |= 0x80;
    }

    return reg;
}

bool DS4432U_test(void)
{
    uint8_t data[3];

    /* Read the DS4432U+ WHO_AM_I register, on power up the register should have the value 0x00 */
    esp_err_t register_result = register_read(DS4432U_SENSOR_ADDR, DS4432U_OUT0_REG, data, 1);
    ESP_LOGI(TAG, "DS4432U+ OUT1 = 0x%02X", data[0]);
    return register_result == ESP_OK;
}

void DS4432U_read(void)
{
    uint8_t data[3];

    /* Read the DS4432U+ WHO_AM_I register, on power up the register should have the value 0x00 */
    ESP_ERROR_CHECK(register_read(DS4432U_SENSOR_ADDR, DS4432U_OUT0_REG, data, 1));
    ESP_LOGI(TAG, "DS4432U+ OUT1 = 0x%02X", data[0]);
}

static void DS4432U_set(uint8_t val)
{
    ESP_LOGI(TAG, "Writing 0x%02X", val);
    ESP_ERROR_CHECK(register_write_byte(DS4432U_SENSOR_ADDR, DS4432U_OUT0_REG, val));
}

bool DS4432U_set_vcore(float core_voltage)
{
    uint8_t reg_setting;

    reg_setting = voltage_to_reg(core_voltage);

    ESP_LOGI(TAG, "Set ASIC voltage = %.3fV [0x%02X]", core_voltage, reg_setting);

    DS4432U_set(reg_setting); /// eek!

    return true;
}
