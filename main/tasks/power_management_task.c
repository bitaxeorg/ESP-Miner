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

static const char * TAG = "power_management";

void POWER_MANAGEMENT_task(void * pvParameters)
{
    ESP_LOGI(TAG, "Starting");

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    SystemModule * sys_module = &GLOBAL_STATE->SYSTEM_MODULE;

    while (1) {
        power_management->voltage = Power_get_input_voltage(GLOBAL_STATE);
        power_management->power = Power_get_power(GLOBAL_STATE);

        power_management->fan_rpm = Thermal_get_fan_speed(&GLOBAL_STATE->DEVICE_CONFIG);
        power_management->chip_temp_avg = Thermal_get_chip_temp(GLOBAL_STATE);
        
        // Only get second temperature for dual-sensor devices (GAMMA_TURBO)
        if (Thermal_has_dual_sensors(&GLOBAL_STATE->DEVICE_CONFIG)) {
            thermal_temps_t temps = Thermal_get_chip_temps(GLOBAL_STATE);
            power_management->chip_temp_avg = temps.temp1;
            power_management->chip_temp2_avg = temps.temp2;
        } else {
            power_management->chip_temp2_avg = 0.0f;
        }

        power_management->vr_temp = Power_get_vreg_temp(GLOBAL_STATE);

        //overheat mode if the voltage regulator or ASIC is too hot
        bool asic_overheat = power_management->chip_temp_avg > THROTTLE_TEMP;
        
        // For EMC2103 devices, check second chip temperature
        if (GLOBAL_STATE->DEVICE_CONFIG.EMC2103) {
            asic_overheat = asic_overheat || (power_management->chip_temp2_avg > THROTTLE_TEMP);
        }
        
        if ((power_management->vr_temp > TPS546_THROTTLE_TEMP || asic_overheat) && (power_management->frequency_value > 50 || power_management->voltage > 1000)) {
            if (GLOBAL_STATE->DEVICE_CONFIG.EMC2103) {
                ESP_LOGE(TAG, "OVERHEAT! VR: %fC ASIC1: %fC ASIC2: %fC", power_management->vr_temp, power_management->chip_temp_avg, power_management->chip_temp2_avg);
            } else {
                ESP_LOGE(TAG, "OVERHEAT! VR: %fC ASIC: %fC", power_management->vr_temp, power_management->chip_temp_avg);
            }
            power_management->fan_perc = 100;
            Thermal_set_fan_percent(&GLOBAL_STATE->DEVICE_CONFIG, 1);

            // Turn off core voltage
            VCORE_set_voltage(GLOBAL_STATE, 0.0f);

            nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, 1000);
            nvs_config_set_u16(NVS_CONFIG_ASIC_FREQUENCY, 50);
            nvs_config_set_float(NVS_CONFIG_ASIC_FREQUENCY_FLOAT, 50);
            nvs_config_set_u16(NVS_CONFIG_FAN_SPEED, 100);
            nvs_config_set_u16(NVS_CONFIG_AUTO_FAN_SPEED, 0);
            nvs_config_set_u16(NVS_CONFIG_OVERHEAT_MODE, 1);
            exit(EXIT_FAILURE);
        }

        // Check for changing of overheat mode
        uint16_t new_overheat_mode = nvs_config_get_u16(NVS_CONFIG_OVERHEAT_MODE, 0);
        
        if (new_overheat_mode != sys_module->overheat_mode) {
            sys_module->overheat_mode = new_overheat_mode;
            ESP_LOGI(TAG, "Overheat mode updated to: %d", sys_module->overheat_mode);
        }

        VCORE_check_fault(GLOBAL_STATE);

        // looper:
        vTaskDelay(POLL_RATE / portTICK_PERIOD_MS);
    }
}
