#include <stdio.h>
#include <string.h>
#include "esp_log.h"

#include "driver/i2c.h"
#include "driver/gpio.h"

#include "led_controller.h"
#include "DS4432U.h"
#include "EMC2101.h"
#include "INA260.h"
#include "adc.h"
#include "oled.h"

static const char *TAG = "system";

#define BM1397_VOLTAGE CONFIG_BM1397_VOLTAGE

void init_system(void) {
    
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
    DS4432U_set_vcore(BM1397_VOLTAGE / 1000.0);
    
    //Fan Tests
    EMC2101_init();
    EMC2101_set_fan_speed(0.75);
    vTaskDelay(500 / portTICK_RATE_MS);

    //oled
    if (!OLED_init()) {
        ESP_LOGI(TAG, "OLED init failed!");
    } else {
        ESP_LOGI(TAG, "OLED init success!");
        //clear the oled screen
        OLED_fill(0);
    }
}

void get_stats(void) {
    char oled_buf[20];

    uint16_t fan_speed = EMC2101_get_fan_speed();
    float chip_temp = EMC2101_get_chip_temp();
    //float current = INA260_read_current();
    //float voltage = INA260_read_voltage();
    float power = INA260_read_power();
    //uint16_t vcore = ADC_get_vcore();

    // ESP_LOGI(TAG, "Fan Speed: %d RPM", fan_speed);
    // ESP_LOGI(TAG, "Chip Temp: %.2f C", chip_temp);

    // //Current Sensor tests
    // ESP_LOGI(TAG, "Current: %.2f mA", current);
    // ESP_LOGI(TAG, "Voltage: %.2f mV", voltage);
    // ESP_LOGI(TAG, "Power: %.2f mW", power);

    // //ESP32 ADC tests
    // ESP_LOGI(TAG, "Vcore: %d mV\n", vcore);

    if (OLED_status()) {
        memset(oled_buf, 0, 20);
        snprintf(oled_buf, 20, "Fan: %d RPM", fan_speed);
        OLED_clearLine(1);
        OLED_writeString(0, 1, oled_buf);

        memset(oled_buf, 0, 20);
        snprintf(oled_buf, 20, "Temp: %.2f C", chip_temp);
        OLED_clearLine(2);
        OLED_writeString(0, 2, oled_buf);

        memset(oled_buf, 0, 20);
        snprintf(oled_buf, 20, "Pwr: %.2f mW", power);
        OLED_clearLine(3);
        OLED_writeString(0, 3, oled_buf);
    }

}

void SysTask(void *arg) {

    init_system();

    while(1){
        get_stats();
        vTaskDelay(5000 / portTICK_RATE_MS);
    }
}