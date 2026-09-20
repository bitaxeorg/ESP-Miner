#include "frequency_transition_bmXX.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include "global_state.h"

#define EPSILON 0.0001f
#define STEP_SIZE 6.25 // MHz step size

static const char * TAG = "frequency_transition";

static bool apply_frequency(GlobalState *state, set_hash_frequency_fn set_frequency, float frequency)
{
    float actual = set_frequency(frequency);
    if (!isfinite(actual) || actual <= 0) {
        ESP_LOGE(TAG, "Frequency change to %g MHz failed; retaining last valid frequency", frequency);
        return false;
    }
    state->POWER_MANAGEMENT_MODULE.actual_frequency = actual;
    return true;
}

void do_frequency_transition(GlobalState * GLOBAL_STATE, set_hash_frequency_fn set_frequency_fn)
{
    float target_frequency = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value;
    float current_frequency = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.actual_frequency;

    if (!isfinite(target_frequency) || target_frequency <= 0 || target_frequency > UINT16_MAX ||
        !isfinite(current_frequency) || current_frequency <= 0 || current_frequency > UINT16_MAX) {
        ESP_LOGE(TAG, "Invalid frequency transition from %g to %g MHz", current_frequency, target_frequency);
        return;
    }

    if (fabs(current_frequency - target_frequency) < EPSILON) {
        return;
    }

    if (fabs(target_frequency - current_frequency) < STEP_SIZE) {
        current_frequency = target_frequency;
        apply_frequency(GLOBAL_STATE, set_frequency_fn, current_frequency);
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
            if (!apply_frequency(GLOBAL_STATE, set_frequency_fn, current_frequency)) {
                return;
            }
            
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
    }
    
    if (fabs(current_frequency - target_frequency) > EPSILON) {
        current_frequency = target_frequency;
        if (!apply_frequency(GLOBAL_STATE, set_frequency_fn, current_frequency)) {
            return;
        }
    }
    
    ESP_LOGI(TAG, "Successfully transitioned to %g MHz", target_frequency);
}
