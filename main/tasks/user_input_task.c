#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h" // Include esp_timer for esp_timer_get_time
#include "driver/gpio.h"
#include "esp_log.h"
#include "connect.h"

#define BUTTON_BOOT GPIO_NUM_0
#define SHORT_PRESS_DURATION_MS 100 // Define what constitutes a short press
#define LONG_PRESS_DURATION_MS 2000 // Define what constitutes a long press

static const char *TAG = "user_input";
static bool button_being_pressed = false;
static int64_t button_press_time = 0;

extern QueueHandle_t user_input_queue; // Declare the queue as external

void USER_INPUT_task(void *pvParameters)
{
    gpio_set_direction(BUTTON_BOOT, GPIO_MODE_INPUT);

    while (1)
    {
        if (gpio_get_level(BUTTON_BOOT) == 0 && button_being_pressed == false)
        { // If button is pressed
            button_being_pressed = true;
            button_press_time = esp_timer_get_time();
        }
        else if (gpio_get_level(BUTTON_BOOT) == 1 && button_being_pressed == true)
        {
            int64_t press_duration = esp_timer_get_time() - button_press_time;
            button_being_pressed = false;
            if (press_duration > LONG_PRESS_DURATION_MS * 1000)
            {
                ESP_LOGI(TAG, "LONG PRESS DETECTED");
                xQueueSend(user_input_queue, (void *)"LONG", (TickType_t)0);
            }
            else if (press_duration > SHORT_PRESS_DURATION_MS * 1000)
            {
                ESP_LOGI(TAG, "SHORT PRESS DETECTED");
                xQueueSend(user_input_queue, (void *)"SHORT", (TickType_t)0);
            }
        }

        vTaskDelay(30 / portTICK_PERIOD_MS); // Add delay so that current task does not starve idle task and trigger watchdog timer
    }
}