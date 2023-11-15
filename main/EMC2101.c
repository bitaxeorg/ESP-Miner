#include "driver/i2c.h"
#include "esp_log.h"
#include <stdio.h>

#include "EMC2101.h"

#define I2C_MASTER_SCL_IO 48 /*!< GPIO number used for I2C master clock */
#define I2C_MASTER_SDA_IO 47 /*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM                                                                                                             \
    0 /*!< I2C master i2c port number, the number of i2c peripheral interfaces available will depend on the chip */
#define I2C_MASTER_FREQ_HZ 400000   /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS 1000

// static const char *TAG = "EMC2101.c";

/**
 * @brief Read a sequence of I2C bytes
 */
static esp_err_t register_read(uint8_t reg_addr, uint8_t * data, size_t len)
{
    return i2c_master_write_read_device(I2C_MASTER_NUM, EMC2101_I2CADDR_DEFAULT, &reg_addr, 1, data, len,
                                        I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

/**
 * @brief Write a byte to a I2C register
 */
static esp_err_t register_write_byte(uint8_t reg_addr, uint8_t data)
{
    int ret;
    uint8_t write_buf[2] = {reg_addr, data};

    ret = i2c_master_write_to_device(I2C_MASTER_NUM, EMC2101_I2CADDR_DEFAULT, write_buf, sizeof(write_buf),
                                     I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);

    return ret;
}

// run this first. sets up the config register
void EMC2101_init(bool invertPolarity)
{

    // set the TACH input
    ESP_ERROR_CHECK(register_write_byte(EMC2101_REG_CONFIG, 0x04));

    if (invertPolarity) {
        ESP_ERROR_CHECK(register_write_byte(EMC2101_FAN_CONFIG, 0b00100011));
    }
}

// takes a fan speed percent
void EMC2101_set_fan_speed(float percent)
{
    uint8_t speed;

    speed = (uint8_t) (63.0 * percent);
    ESP_ERROR_CHECK(register_write_byte(EMC2101_REG_FAN_SETTING, speed));
}

// RPM = 5400000/reading
uint16_t EMC2101_get_fan_speed(void)
{
    uint8_t tach_lsb, tach_msb;
    uint16_t reading;
    uint16_t RPM;

    ESP_ERROR_CHECK(register_read(EMC2101_TACH_LSB, &tach_lsb, 1));
    ESP_ERROR_CHECK(register_read(EMC2101_TACH_MSB, &tach_msb, 1));

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

    ESP_ERROR_CHECK(register_read(EMC2101_EXTERNAL_TEMP_MSB, &temp_msb, 1));
    ESP_ERROR_CHECK(register_read(EMC2101_EXTERNAL_TEMP_LSB, &temp_lsb, 1));

    reading = temp_lsb | (temp_msb << 8);
    reading >>= 5;

    return (float) reading / 8.0;
}

uint8_t EMC2101_get_internal_temp(void)
{
    uint8_t temp;
    ESP_ERROR_CHECK(register_read(EMC2101_INTERNAL_TEMP, &temp, 1));
    return temp;
}
