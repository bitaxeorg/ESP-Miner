#include "esp_event.h"
#include "esp_log.h"
#include "i2c_bitaxe.h"

#define I2C_MASTER_SCL_IO 48        /*!< GPIO number used for I2C master clock */
#define I2C_MASTER_SDA_IO 47        /*!< GPIO number used for I2C master data  */

#define I2C_MASTER_NUM 0            /*!< I2C master i2c port number, the number of i2c peripheral interfaces available will depend on the chip */
#define I2C_MASTER_TIMEOUT_MS 1000

//#define I2C_DEFAULT_TIMEOUT ( I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS )
#define I2C_DEFAULT_TIMEOUT -1  //-1 means wait forever

static i2c_master_bus_handle_t i2c_bus_handle;

/**
 * @brief i2c master initialization
 */
esp_err_t i2c_bitaxe_init(void)
{
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_MASTER_NUM,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    return i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle);
}

/**
 * @brief Add a new I2C Device
 * @param device_address The I2C device address
 * @param dev_handle The I2C device handle
 */
esp_err_t i2c_bitaxe_add_device(uint8_t device_address, i2c_master_dev_handle_t * dev_handle)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = device_address,
        .scl_speed_hz = I2C_BUS_SPEED_HZ,
    };

    return i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, dev_handle);
}

esp_err_t i2c_bitaxe_get_master_bus_handle(i2c_master_bus_handle_t * dev_handle)
{
    *dev_handle = i2c_bus_handle;
    return ESP_OK;
}

/**
 * @brief Read a sequence of I2C bytes
 * @param dev_handle The I2C device handle
 * @param reg_addr The register address to read from
 * @param read_buf The buffer to store the read data
 * @param len The number of bytes to read
 */
esp_err_t i2c_bitaxe_register_read(i2c_master_dev_handle_t dev_handle, uint8_t reg_addr, uint8_t * read_buf, size_t len)
{
    // return i2c_master_write_read_device(I2C_MASTER_NUM, device_address, &reg_addr, 1, data, len, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    //ESP_LOGI("I2C", "Reading %d bytes from register 0x%02X", len, reg_addr);

    return i2c_master_transmit_receive(dev_handle, &reg_addr, 1, read_buf, len, I2C_DEFAULT_TIMEOUT);
}

/**
 * @brief Write a byte to a I2C register
 * @param dev_handle The I2C device handle
 * @param reg_addr The register address to write to
 * @param data The data to write
 */
esp_err_t i2c_bitaxe_register_write_byte(i2c_master_dev_handle_t dev_handle, uint8_t reg_addr, uint8_t data)
{
    uint8_t write_buf[2] = {reg_addr, data};

    //return i2c_master_write_to_device(I2C_MASTER_NUM, device_address, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);

    return i2c_master_transmit(dev_handle, write_buf, 2, I2C_DEFAULT_TIMEOUT);
}

/**
 * @brief Write a bytes to a I2C register
 * @param dev_handle The I2C device handle
 * @param data The data to write
 * @param len The number of bytes to write
 */
esp_err_t i2c_bitaxe_register_write_bytes(i2c_master_dev_handle_t dev_handle, uint8_t * data, uint8_t len)
{
    return i2c_master_transmit(dev_handle, data, len, I2C_DEFAULT_TIMEOUT);
}

/**
 * @brief Write a word to a I2C register
 * @param dev_handle The I2C device handle
 * @param reg_addr The register address to write to
 * @param data The data to write
 */
esp_err_t i2c_bitaxe_register_write_word(i2c_master_dev_handle_t dev_handle, uint8_t reg_addr, uint16_t data)
{
    uint8_t write_buf[3] = {reg_addr, (uint8_t)(data & 0x00FF), (uint8_t)((data & 0xFF00) >> 8)};

    //return i2c_master_write_to_device(I2C_MASTER_NUM, device_address, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);

    return i2c_master_transmit(dev_handle, write_buf, 3, I2C_DEFAULT_TIMEOUT);
}
