#include <stdio.h>
#include <math.h>
#include "esp_log.h"
#include "driver/i2c.h"

#define I2C_MASTER_SCL_IO 48        /*!< GPIO number used for I2C master clock */
#define I2C_MASTER_SDA_IO 47        /*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM 0            /*!< I2C master i2c port number, the number of i2c peripheral interfaces available will depend on the chip */
#define I2C_MASTER_FREQ_HZ 400000   /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS 1000

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

/**
 * @brief i2c master initialization
 */
esp_err_t i2c_master_init(void)
{
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
esp_err_t i2c_master_delete(void)
{
    return i2c_driver_delete(I2C_MASTER_NUM);
}

/**
 * @brief Read a sequence of I2C bytes
 */
static esp_err_t register_read(uint8_t reg_addr, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(I2C_MASTER_NUM, DS4432U_SENSOR_ADDR, &reg_addr, 1, data, len, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

/**
 * @brief Write a byte to a I2C register
 */
static esp_err_t register_write_byte(uint8_t reg_addr, uint8_t data)
{
    int ret;
    uint8_t write_buf[2] = {reg_addr, data};

    ret = i2c_master_write_to_device(I2C_MASTER_NUM, DS4432U_SENSOR_ADDR, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);

    return ret;
}

void DS4432U_read(void)
{
    uint8_t data[3];

    /* Read the DS4432U+ WHO_AM_I register, on power up the register should have the value 0x00 */
    ESP_ERROR_CHECK(register_read(DS4432U_OUT0_REG, data, 1));
    ESP_LOGI(TAG, "DS4432U+ OUT1 = 0x%02X", data[0]);
}

static void DS4432U_set(uint8_t val)
{
    ESP_LOGI(TAG, "Writing 0x%02X", val);
    ESP_ERROR_CHECK(register_write_byte(DS4432U_OUT0_REG, val));
}

bool DS4432U_set_vcore(float core_voltage)
{
    uint8_t reg_setting;

    reg_setting = voltage_to_reg(core_voltage);

    ESP_LOGI(TAG, "Set BM1397 voltage = %.3fV [0x%02X]", core_voltage, reg_setting);

    DS4432U_set(reg_setting); /// eek!

    return true;
}