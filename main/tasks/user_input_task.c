#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "connect.h"

#define BUTTON_BOOT GPIO_NUM_0

static const char *TAG = "user_input";
static bool button_being_pressed = false;

void USER_INPUT_task(void *pvParameters)
{

    gpio_set_direction(BUTTON_BOOT, GPIO_MODE_INPUT);

    while (1)
    {
        if (gpio_get_level(BUTTON_BOOT) == 0 && button_being_pressed == false)
        { // If button is pressed
            ESP_LOGI(TAG, "BUTTON PRESSED");
            button_being_pressed = true;
            toggle_wifi_softap();
        }
        else if (gpio_get_level(BUTTON_BOOT) == 1 && button_being_pressed == true)
        {
            button_being_pressed = false;
        }

        vTaskDelay(30 / portTICK_PERIOD_MS); // Add delay so that current task does not starve idle task and trigger watchdog timer
    }
}