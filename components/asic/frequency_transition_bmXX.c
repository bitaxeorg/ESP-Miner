#include "frequency_transition_bmXX.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

const char *FREQUENCY_TRANSITION_TAG = "frequency_transition";

static float current_frequency = 56.25;

bool do_frequency_transition(float target_frequency, set_hash_frequency_fn set_frequency_fn, int asic_type) {
    if (set_frequency_fn == NULL) {
        ESP_LOGE(FREQUENCY_TRANSITION_TAG, "Invalid function pointer provided");
        return false;
    }

    float step = 6.25;
    float current = current_frequency;
    float target = target_frequency;

    float direction = (target > current) ? step : -step;

    // If current frequency is not a multiple of step, adjust to the nearest multiple
    if (fmod(current, step) != 0) {
        float next_dividable;
        if (direction > 0) {
            next_dividable = ceil(current / step) * step;
        } else {
            next_dividable = floor(current / step) * step;
        }
        current = next_dividable;
        
        // Call the provided hash frequency function
        set_frequency_fn(current);
        
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    // Gradually adjust frequency in steps until target is reached
    while ((direction > 0 && current < target) || (direction < 0 && current > target)) {
        float next_step = fmin(fabs(direction), fabs(target - current));
        current += direction > 0 ? next_step : -next_step;
        
        // Call the provided hash frequency function
        set_frequency_fn(current);
        
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    
    // Set the final target frequency
    set_frequency_fn(target);
    current_frequency = target;
    
    ESP_LOGI(FREQUENCY_TRANSITION_TAG, "Successfully transitioned ASIC type %d to %.2f MHz", asic_type, target);
    return true;
}
