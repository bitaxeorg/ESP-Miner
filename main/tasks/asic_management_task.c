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

#define POLL_RATE 1800
#define THROTTLE_TEMP 75.0

#define TPS546_THROTTLE_TEMP 105.0

static const char * TAG = "asic_management";

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

    ASIC_MANAGEMENT_init_frequency(power_management);
    
    float last_asic_frequency = 50;

    uint16_t last_core_voltage = 0.0;

    while (1) {
        uint16_t core_voltage = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, CONFIG_ASIC_VOLTAGE);
        float asic_frequency = nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY_FLOAT, CONFIG_ASIC_FREQUENCY);

        if (core_voltage != last_core_voltage) {
            ESP_LOGI(TAG, "setting new vcore voltage to %umV", core_voltage);
            VCORE_set_voltage(GLOBAL_STATE, (double) core_voltage / 1000.0);
            last_core_voltage = core_voltage;
        }

        if (asic_frequency != last_asic_frequency) {
            ESP_LOGI(TAG, "New ASIC frequency requested: %g MHz (current: %g MHz)", asic_frequency, last_asic_frequency);
            
            bool success = ASIC_set_frequency(GLOBAL_STATE, asic_frequency);
            
            if (success) {
                power_management->frequency_value = asic_frequency;
            }
            
            last_asic_frequency = asic_frequency;
        }

        // looper:
        vTaskDelay(POLL_RATE / portTICK_PERIOD_MS);        
    }        
}
