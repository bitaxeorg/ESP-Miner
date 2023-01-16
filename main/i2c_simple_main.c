/* i2c - Simple example

   Simple I2C example that shows how to initialize I2C
   as well as reading and writing from and to registers for a sensor connected over I2C.

   For other examples please check:
   https://github.com/espressif/esp-idf/tree/master/examples

   See README.md file to get detailed usage of this example.

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c.h"

#include "led_controller.h"
#include "DS4432U.h"

static const char *TAG = "i2c-test";

//INA260 -- Power meter
//address: 0x40
#define INA260_SENSOR_ADDR                  0x40        //Slave address of the INA260
#define INA260_MANFID_REG                   0xFE        //should be 0x5449

//EMC2101 -- Fan and temp sensor controller
//address: 0x4C
#define EMC2101_SENSOR_ADDR                 0x4C        //Slave address of the EMC2101
#define EMC2101_PRODUCTID_REG               0xFD        //should be 0x16 or 0x28





void app_main(void) {

    //test the LEDs
    ESP_LOGI(TAG, "Init LEDs!");
    initLEDs();
    // gpio_set_level(LEDX_R, 1);
    // gpio_set_level(LEDZ_R, 1);

    ledc_init();
    led_set();

    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "I2C initialized successfully");

    DS4432U_read();

    // /* Read the EMC2101 WHO_AM_I register, on power up the register should have the value 0x16 or 0x28 */
    // ESP_ERROR_CHECK(register_read(EMC2101_SENSOR_ADDR, EMC2101_PRODUCTID_REG, data, 1));
    // ESP_LOGI(TAG, "EMC2101 PRODUCT ID = 0x%02X", data[0]);

    // /* Read the INA260 WHO_AM_I register, on power up the register should have the value 0x5449 */
    // ESP_ERROR_CHECK(register_read(INA260_SENSOR_ADDR, INA260_MANFID_REG, data, 2));
    // ESP_LOGI(TAG, "INA260 MANF ID = 0x%02X%02X", data[0], data[1]);

    // /* Read the DS4432U+ WHO_AM_I register, on power up the register should have the value 0x00 */
    // ESP_ERROR_CHECK(register_read(DS4432U_SENSOR_ADDR, DS4432U_OUT1_REG, data, 1));
    // ESP_LOGI(TAG, "DS4432U+ OUT1 = 0x%02X", data[0]);

    // /* Demonstrate writing by reseting the MPU9250 */
    // ESP_ERROR_CHECK(mpu9250_register_write_byte(MPU9250_PWR_MGMT_1_REG_ADDR, 1 << MPU9250_RESET_BIT));

    ESP_ERROR_CHECK(i2c_master_delete());
    ESP_LOGI(TAG, "I2C unitialized successfully");
}
