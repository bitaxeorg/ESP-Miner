#include "frequency_transition_bmXX.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include "global_state.h"

#define EPSILON 0.0001f
#define STEP_SIZE 6.25 // MHz step size

static const char * TAG = "frequency_transition";

void do_frequency_transition(void * pvParameters, set_hash_frequency_fn set_frequency_fn)
{
    GlobalState * GLOBAL_STATE = (GlobalState *)pvParameters;
    float target_frequency = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value;
    float current_frequency = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.actual_frequency;

    if (fabs(current_frequency - target_frequency) < EPSILON) {
        return;
    }

    if (fabs(target_frequency - current_frequency) < STEP_SIZE) {
        current_frequency = target_frequency;
        GLOBAL_STATE->POWER_MANAGEMENT_MODULE.actual_frequency = set_frequency_fn(current_frequency);
        return;
    }

    ESP_LOGI(TAG, "Ramping up frequency from %g MHz to %g MHz", current_frequency, target_frequency);

    int current_step = (target_frequency > current_frequency) ? (int)floor(current_frequency / STEP_SIZE) : (int)ceil(current_frequency / STEP_SIZE);
    int target_step = (target_frequency > current_frequency) ? (int)floor(target_frequency / STEP_SIZE) : (int)ceil(target_frequency / STEP_SIZE);

    if (current_step != target_step) {
        int signum = (target_frequency > current_frequency) ? 1 : -1;
        
        while ((signum > 0 && current_step < target_step) ||
               (signum < 0 && current_step > target_step)) {
            current_step += signum;

            current_frequency = current_step * STEP_SIZE;
            GLOBAL_STATE->POWER_MANAGEMENT_MODULE.actual_frequency = set_frequency_fn(current_frequency);
            
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
    }
    
    if (fabs(current_frequency - target_frequency) > EPSILON) {
        current_frequency = target_frequency;
        GLOBAL_STATE->POWER_MANAGEMENT_MODULE.actual_frequency = set_frequency_fn(current_frequency);
    }
    
    ESP_LOGI(TAG, "Successfully transitioned to %g MHz", target_frequency);
}
