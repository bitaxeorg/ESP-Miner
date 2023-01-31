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
#include "driver/gpio.h"

// Include FreeRTOS for delay
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "led_controller.h"
#include "DS4432U.h"
#include "EMC2101.h"
#include "INA260.h"
#include "adc.h"

static const char *TAG = "i2c-test";

void app_main(void) {

    //test the LEDs
    // ESP_LOGI(TAG, "Init LEDs!");
    // ledc_init();
    // led_set();

    //Playing with BI level
    gpio_set_direction(GPIO_NUM_10, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_10, 0);

    //Init I2C
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "I2C initialized successfully");

    ADC_init();

    //DS4432U tests
    DS4432U_set_vcore(1.4);
    
    //Fan Tests
    EMC2101_init();
    EMC2101_set_fan_speed(0.5);
    vTaskDelay(500 / portTICK_RATE_MS);

    while (1) {
        ESP_LOGI(TAG, "Fan Speed: %d RPM", EMC2101_get_fan_speed());
        ESP_LOGI(TAG, "Chip Temp: %.2f C", EMC2101_get_chip_temp());

        //Current Sensor tests
        ESP_LOGI(TAG, "Current: %.2f mA", INA260_read_current());
        ESP_LOGI(TAG, "Voltage: %.2f mV", INA260_read_voltage());
        ESP_LOGI(TAG, "Power: %.2f mW", INA260_read_power());

        //ESP32 ADC tests
        ESP_LOGI(TAG, "Vcore: %d mV\n", ADC_get_vcore());

        vTaskDelay(5000 / portTICK_RATE_MS);
    }

    ESP_ERROR_CHECK(i2c_master_delete());
    ESP_LOGI(TAG, "I2C unitialized successfully");
}
