#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c.h"

#define I2C_MASTER_SCL_IO           CONFIG_I2C_MASTER_SCL      /*!< GPIO number used for I2C master clock */
#define I2C_MASTER_SDA_IO           CONFIG_I2C_MASTER_SDA      /*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM              0                          /*!< I2C master i2c port number, the number of i2c peripheral interfaces available will depend on the chip */
#define I2C_MASTER_FREQ_HZ          400000                     /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE   0                          /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0                          /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS       1000

//EMC2101 -- Fan and temp sensor controller
//address: 0x4C
#define EMC2101_SENSOR_ADDR                 0x4C        //Slave address of the EMC2101
#define EMC2101_PRODUCTID_REG               0xFD        //should be 0x16 or 0x28
#define EMC2101_CONFIG_REG                  0x03

#define EMC2101_FANCONFIG_REG               0x4A
#define EMC2101_FANSETTING_REG              0x4C

#define EMC2101_TACHREADING_REG             0x46 //2 bytes

static const char *TAG = "EMC2101.c";

/**
 * @brief Read a sequence of I2C bytes
 */
static esp_err_t register_read(uint8_t reg_addr, uint8_t *data, size_t len) {
    return i2c_master_write_read_device(I2C_MASTER_NUM, EMC2101_SENSOR_ADDR, &reg_addr, 1, data, len, I2C_MASTER_TIMEOUT_MS / portTICK_RATE_MS);
}

/**
 * @brief Write a byte to a I2C register
 */
static esp_err_t register_write_byte(uint8_t reg_addr, uint8_t data) {
    int ret;
    uint8_t write_buf[2] = {reg_addr, data};

    ret = i2c_master_write_to_device(I2C_MASTER_NUM, EMC2101_SENSOR_ADDR, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS / portTICK_RATE_MS);

    return ret;
}

//takes a fan speed percent
void EMC2101_set_config(uint8_t reg) {
    ESP_ERROR_CHECK(register_write_byte(EMC2101_CONFIG_REG, reg));
}

//takes a fan speed percent
void EMC2101_set_fan_speed(float percent) {
    uint8_t speed;

    speed = (uint8_t)(63.0 * percent);
    ESP_ERROR_CHECK(register_write_byte(EMC2101_FANSETTING_REG, speed));
}

//RPM = 5400000/reading
uint32_t EMC2101_get_fan_speed(void) {
    uint8_t data[3];
    uint16_t reading;
    uint32_t RPM;

    ESP_ERROR_CHECK(register_read(EMC2101_TACHREADING_REG, data, 2));

    ESP_LOGI(TAG, "Raw Fan Speed = %02X %02X", data[0], data[1]);

    reading = data[0] | (data[1] << 8);
    RPM = 5400000/reading;

    ESP_LOGI(TAG, "Fan Speed = %d RPM", RPM);
    return RPM;
}

void EMC2101_read(void) {
    uint8_t data[3];

    /* Read the EMC2101 WHO_AM_I register, on power up the register should have the value 0x16 or 0x28 */
    ESP_ERROR_CHECK(register_read(EMC2101_PRODUCTID_REG, data, 1));
    ESP_LOGI(TAG, "EMC2101 PRODUCT ID = 0x%02X", data[0]);
}