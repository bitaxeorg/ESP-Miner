#include "frequency_transition_bmXX.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include "global_state.h"

#define EPSILON 0.0001f
#define STEP_SIZE 6.25 // MHz step size

static const char * TAG = "frequency_transition";

static bool set_step(GlobalState * GLOBAL_STATE, set_hash_frequency_fn set_frequency_fn, float frequency)
{
    float actual_frequency = set_frequency_fn(frequency);
    if (!isfinite(actual_frequency) || actual_frequency <= 0.0f) {
        ESP_LOGE(TAG, "Failed to set frequency to %g MHz", frequency);
        return false;
    }
    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.actual_frequency = actual_frequency;
    return true;
}

bool do_frequency_transition(GlobalState * GLOBAL_STATE, set_hash_frequency_fn set_frequency_fn)
{
    float target_frequency = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value;
    float current_frequency = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.actual_frequency;

    if (!isfinite(target_frequency) || target_frequency <= 0.0f ||
        !isfinite(current_frequency) || current_frequency <= 0.0f) {
        return false;
    }

    if (fabs(current_frequency - target_frequency) < EPSILON) {
        return true;
    }

    if (fabs(target_frequency - current_frequency) < STEP_SIZE) {
        current_frequency = target_frequency;
        return set_step(GLOBAL_STATE, set_frequency_fn, current_frequency);
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
            if (!set_step(GLOBAL_STATE, set_frequency_fn, current_frequency)) {
                return false;
            }
            
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
    }
    
    if (fabs(current_frequency - target_frequency) > EPSILON) {
        current_frequency = target_frequency;
        if (!set_step(GLOBAL_STATE, set_frequency_fn, current_frequency)) {
            return false;
        }
    }
    
    ESP_LOGI(TAG, "Successfully transitioned to %g MHz", target_frequency);
    return true;
}
