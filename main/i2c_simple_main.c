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

void app_main(void) {

    //test the LEDs
    ESP_LOGI(TAG, "Init LEDs!");

    ledc_init();
    led_set();

    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "I2C initialized successfully");

    DS4432U_read();

    DS4432U_set(0x00);
    float core_voltage = 1.0;
    uint8_t reg_setting;

    reg_setting = voltage_to_reg(core_voltage);

    ESP_LOGI(TAG, "Test set %.3fV = 0x%02X", core_voltage, reg_setting);

    DS4432U_set(reg_setting); ///eek!

    ESP_ERROR_CHECK(i2c_master_delete());
    ESP_LOGI(TAG, "I2C unitialized successfully");
}
