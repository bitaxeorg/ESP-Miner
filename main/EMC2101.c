#include "esp_log.h"
#include <stdio.h>

#include "EMC2101.h"

// static const char *TAG = "EMC2101.c";

// run this first. sets up the config register
void EMC2101_init(bool invertPolarity)
{

    // set the TACH input
    ESP_ERROR_CHECK(register_write_byte(EMC2101_I2CADDR_DEFAULT, EMC2101_REG_CONFIG, 0x04));

    if (invertPolarity) {
        ESP_ERROR_CHECK(register_write_byte(EMC2101_I2CADDR_DEFAULT, EMC2101_FAN_CONFIG, 0b00100011));
    }
}

// takes a fan speed percent
void EMC2101_set_fan_speed(float percent)
{
    uint8_t speed;

    speed = (uint8_t) (63.0 * percent);
    ESP_ERROR_CHECK(register_write_byte(EMC2101_I2CADDR_DEFAULT, EMC2101_REG_FAN_SETTING, speed));
}

// RPM = 5400000/reading
uint16_t EMC2101_get_fan_speed(void)
{
    uint8_t tach_lsb, tach_msb;
    uint16_t reading;
    uint16_t RPM;

    ESP_ERROR_CHECK(register_read(EMC2101_I2CADDR_DEFAULT, EMC2101_TACH_LSB, &tach_lsb, 1));
    ESP_ERROR_CHECK(register_read(EMC2101_I2CADDR_DEFAULT, EMC2101_TACH_MSB, &tach_msb, 1));

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
    uint8_t temp_msb, temp_lsb;
    uint16_t reading;

    ESP_ERROR_CHECK(register_read(EMC2101_I2CADDR_DEFAULT, EMC2101_EXTERNAL_TEMP_MSB, &temp_msb, 1));
    ESP_ERROR_CHECK(register_read(EMC2101_I2CADDR_DEFAULT, EMC2101_EXTERNAL_TEMP_LSB, &temp_lsb, 1));

    reading = temp_lsb | (temp_msb << 8);
    reading >>= 5;

    return (float) reading / 8.0;
}

uint8_t EMC2101_get_internal_temp(void)
{
    uint8_t temp;
    ESP_ERROR_CHECK(register_read(EMC2101_I2CADDR_DEFAULT, EMC2101_INTERNAL_TEMP, &temp, 1));
    return temp;
}
