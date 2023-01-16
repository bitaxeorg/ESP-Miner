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


//DS4432U+ -- Adjustable current DAC for use with the TPS40305 voltage regulator
//address: 0x90
#define DS4432U_SENSOR_ADDR                 0x48        //Slave address of the DS4432U+
#define DS4432U_OUT0_REG                    0xF8        //register for current output 0
#define DS4432U_OUT1_REG                    0xF9        //register for current output 1

static const char *TAG = "DS4432U.c";

/**
 * @brief i2c master initialization
 */
esp_err_t i2c_master_init(void) {
    int i2c_master_port = I2C_MASTER_NUM;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    i2c_param_config(i2c_master_port, &conf);

    return i2c_driver_install(i2c_master_port, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
}

/**
 * @brief i2c master delete
 */
esp_err_t i2c_master_delete(void) {
    return i2c_driver_delete(I2C_MASTER_NUM);
}


/**
 * @brief Read a sequence of I2C bytes
 */
static esp_err_t register_read(uint8_t i2c_address, uint8_t reg_addr, uint8_t *data, size_t len) {
    return i2c_master_write_read_device(I2C_MASTER_NUM, i2c_address, &reg_addr, 1, data, len, I2C_MASTER_TIMEOUT_MS / portTICK_RATE_MS);
}

/**
 * @brief Write a byte to a I2C register
 */
// static esp_err_t register_write_byte(uint8_t i2c_address, uint8_t reg_addr, uint8_t data) {
//     int ret;
//     uint8_t write_buf[2] = {reg_addr, data};

//     ret = i2c_master_write_to_device(I2C_MASTER_NUM, i2c_address, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS / portTICK_RATE_MS);

//     return ret;
// }

void DS4432U_read(void) {
    uint8_t data[3];
    
    /* Read the DS4432U+ WHO_AM_I register, on power up the register should have the value 0x00 */
    ESP_ERROR_CHECK(register_read(DS4432U_SENSOR_ADDR, DS4432U_OUT1_REG, data, 1));
    ESP_LOGI(TAG, "DS4432U+ OUT1 = 0x%02X", data[0]);
}