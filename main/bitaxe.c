#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "driver/i2c.h"
#include "driver/gpio.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "system.h"
#include "serial.h"

static const char *TAG = "main";

TaskHandle_t sysTaskHandle = NULL;
TaskHandle_t serialTaskHandle = NULL;

void app_main(void) {
    ESP_LOGI(TAG, "Welcome to the bitaxe!");

    xTaskCreate(SysTask, "System_Task", 4096, NULL, 10, &sysTaskHandle);
    xTaskCreate(SerialTask, "serial_test", 4096, NULL, 10, &serialTaskHandle);
    
}
