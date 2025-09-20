#include <string.h>
#include "INA260.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_state.h"
#include "math.h"
#include "mining.h"
#include "nvs_config.h"
#include "serial.h"
#include "TPS546.h"
#include "vcore.h"
#include "thermal.h"
#include "PID.h"
#include "power.h"
#include "asic.h"

#define POLL_RATE 100
#define THROTTLE_TEMP 75.0

#define TPS546_THROTTLE_TEMP 105.0

#define EPSILON 0.0001f
#define STEP_SIZE 6.25 // MHz step size
#define START_FREQUENCY 50.0 // MHz

static const char * TAG = "asic_management";

static uint16_t current_voltage;
static float current_frequency;
static bool is_frequency_transitioning;

void ASIC_MANAGEMENT_init_frequency(PowerManagementModule * power_management)
{
    float frequency = nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY_FLOAT, -1);
    if (frequency < 0) { // fallback if the float value is not yet set
        frequency = (float) nvs_config_get_u16(NVS_CONFIG_ASIC_FREQUENCY, CONFIG_ASIC_FREQUENCY);

        nvs_config_set_float(NVS_CONFIG_ASIC_FREQUENCY_FLOAT, frequency);
    }

    ESP_LOGI(TAG, "ASIC Frequency: %g MHz", frequency);

    power_management->frequency_value = frequency;
}

void ASIC_MANAGEMENT_task(void * pvParameters)
{
    ESP_LOGI(TAG, "Starting");

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;

    while (1) {
        uint16_t target_voltage = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, CONFIG_ASIC_VOLTAGE);
        if (target_voltage != current_voltage) {
            ESP_LOGI(TAG, "Setting new vcore voltage to %umV", target_voltage);
            VCORE_set_voltage(GLOBAL_STATE, (double) target_voltage / 1000.0);
            current_voltage = target_voltage;
        }
        
        float target_frequency = nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY_FLOAT, CONFIG_ASIC_FREQUENCY);
        if (fabs(current_frequency - target_frequency) < EPSILON) {
            if (is_frequency_transitioning) {
                ESP_LOGI(TAG, "Successfully transitioned to %g MHz", target_frequency);
                is_frequency_transitioning = false;
            }
        } else {
            if (!is_frequency_transitioning) {
                if (current_frequency == 0) {
                    current_frequency = START_FREQUENCY;
                    ESP_LOGI(TAG, "Ramping up frequency from %g MHz to %g MHz", current_frequency, target_frequency);
                } else {
                    ESP_LOGI(TAG, "New ASIC frequency requested: %g MHz (current: %g MHz)", target_frequency, current_frequency);
                }
                is_frequency_transitioning = true;
            }
    
            if (fabs(target_frequency - current_frequency) < STEP_SIZE) {
                current_frequency = target_frequency;
            } else {
                if (target_frequency > current_frequency) {
                    current_frequency = (floor(current_frequency / STEP_SIZE) + 1) * STEP_SIZE;
                } else {
                    current_frequency = (ceil(current_frequency / STEP_SIZE) - 1) * STEP_SIZE;

                }
            }
    
            ASIC_set_frequency(GLOBAL_STATE, current_frequency);
            power_management->frequency_value = current_frequency;
        }      

        // looper:
        vTaskDelay(POLL_RATE / portTICK_PERIOD_MS);        
    }        
}
