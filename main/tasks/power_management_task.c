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

#define POLL_RATE 2000
#define MAX_TEMP 90.0
#define THROTTLE_TEMP 75.0
#define THROTTLE_TEMP_RANGE (MAX_TEMP - THROTTLE_TEMP)

#define VOLTAGE_START_THROTTLE 4900
#define VOLTAGE_MIN_THROTTLE 3500
#define VOLTAGE_RANGE (VOLTAGE_START_THROTTLE - VOLTAGE_MIN_THROTTLE)

#define TPS546_THROTTLE_TEMP 105.0
#define TPS546_MAX_TEMP 145.0

static const char * TAG = "power_management";


void POWER_MANAGEMENT_task(void * pvParameters)
{
    ESP_LOGI(TAG, "Starting");

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    SystemModule * sys_module = &GLOBAL_STATE->SYSTEM_MODULE;

    // PID Controller Initialization
    PIDController fan_pid;
    PID_init(
        &fan_pid,
        5.0,    // Kp (Proportional gain) - 5% fan speed increase per degree above setpoint
        0.5,    // Ki (Integral gain) - Accumulate error over time for steady-state correction
        0.2,    // Kd (Derivative gain) - Respond to rate of temperature change
        THROTTLE_TEMP - 15.0,  // Setpoint (60°C)
        25.0,   // Minimum fan speed (%)
        100.0,  // Maximum fan speed (%)
        true    // Use inverse control for fan (higher temp = higher fan speed)
    );
    
    // Reset integral term to zero to start fresh
    PID_reset(&fan_pid);

    power_management->frequency_multiplier = 1;

    //int last_frequency_increase = 0;
    //uint16_t frequency_target = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, CONFIG_ASIC_FREQUENCY);

    vTaskDelay(500 / portTICK_PERIOD_MS);
    uint16_t last_core_voltage = 0.0;
    uint16_t last_asic_frequency = power_management->frequency_value;
    
    while (1) {

        power_management->voltage = Power_get_input_voltage(GLOBAL_STATE);
        power_management->power = Power_get_power(GLOBAL_STATE);

        power_management->fan_rpm = Thermal_get_fan_speed(GLOBAL_STATE->device_model);
        power_management->chip_temp_avg = Thermal_get_chip_temp(GLOBAL_STATE);

        power_management->vr_temp = Power_get_vreg_temp(GLOBAL_STATE);


        // ASIC Thermal Diode will give bad readings if the ASIC is turned off
        // if(power_management->voltage < tps546_config.TPS546_INIT_VOUT_MIN){
        //     goto looper;
        // }

        //overheat mode if the voltage regulator or ASIC is too hot
        if ((power_management->vr_temp > TPS546_THROTTLE_TEMP || power_management->chip_temp_avg > THROTTLE_TEMP) && (power_management->frequency_value > 50 || power_management->voltage > 1000)) {
            ESP_LOGE(TAG, "OVERHEAT! VR: %fC ASIC %fC", power_management->vr_temp, power_management->chip_temp_avg );
            power_management->fan_perc = 100;
            Thermal_set_fan_percent(GLOBAL_STATE->device_model, 1);

            // Turn off core voltage
            Power_disable(GLOBAL_STATE);

            nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, 1000);
            nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, 50);
            nvs_config_set_u16(NVS_CONFIG_FAN_SPEED, 100);
            nvs_config_set_u16(NVS_CONFIG_AUTO_FAN_SPEED, 0);
            nvs_config_set_u16(NVS_CONFIG_OVERHEAT_MODE, 1);
            exit(EXIT_FAILURE);
        }
        
        if (nvs_config_get_u16(NVS_CONFIG_AUTO_FAN_SPEED, 1) == 1) {
            // Calculate error for logging
            float error = power_management->chip_temp_avg - fan_pid.setpoint;
            
            // PID-based fan control
            float fan_speed_percent = PID_compute(
                &fan_pid, 
                power_management->chip_temp_avg, 
                POLL_RATE / 1000.0  // Convert milliseconds to seconds
            );
            
            // True fallback mechanism: Only activate if PID controller isn't responding properly
            // This is a safety measure for extreme cases
            bool fallback_active = false;
            
            // If temperature is significantly above setpoint but fan speed is still at minimum,
            // or if temperature is very high, activate fallback
            if ((error > 2.0f && fan_speed_percent <= 36.0f) || error > 10.0f) {
                // Calculate a linear fan speed based on temperature
                float linear_fan_speed = 35.0f + (error * 5.0f);  // 5% increase per degree above setpoint
                linear_fan_speed = fminf(linear_fan_speed, 100.0f);  // Cap at 100%
                
                // Use the linear calculation instead of PID output
                fan_speed_percent = linear_fan_speed;
                fallback_active = true;
                
                ESP_LOGW(TAG, "FALLBACK ACTIVATED: PID not responding properly, using linear control");
            }
            
            // Detailed debug log to see what's happening
            //ESP_LOGI(TAG, "PID: Temp=%.1f°C, Setpoint=%.1f°C, Error=%.1f°C, Fan=%.1f%%, P=%.1f, I=%.1f%s", 
            //         power_management->chip_temp_avg, 
            //         fan_pid.setpoint,
            //         error,
            //         fan_speed_percent,
            //         fan_pid.kp * error,  // P term
            //         fan_pid.ki * fan_pid.integral,  // I term
            //         fallback_active ? " (FALLBACK)" : ""
            //        );

            power_management->fan_perc = fan_speed_percent;
            Thermal_set_fan_percent(GLOBAL_STATE->device_model, fan_speed_percent / 100.0);
        } else {
            // Manual fan speed setting (existing logic)
            float fs = (float) nvs_config_get_u16(NVS_CONFIG_FAN_SPEED, 100);
            power_management->fan_perc = fs;
            Thermal_set_fan_percent(GLOBAL_STATE->device_model, (float) fs / 100.0);
        }

        // New voltage and frequency adjustment code
        uint16_t core_voltage = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, CONFIG_ASIC_VOLTAGE);
        uint16_t asic_frequency = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, CONFIG_ASIC_FREQUENCY);

        if (core_voltage != last_core_voltage) {
            ESP_LOGI(TAG, "setting new vcore voltage to %umV", core_voltage);
            VCORE_set_voltage((double) core_voltage / 1000.0, GLOBAL_STATE);
            last_core_voltage = core_voltage;
        }

        if (asic_frequency != last_asic_frequency) {
            ESP_LOGI(TAG, "New ASIC frequency requested: %uMHz (current: %uMHz)", asic_frequency, last_asic_frequency);
            
            bool success = ASIC_set_frequency(GLOBAL_STATE, (float)asic_frequency);
            
            if (success) {
                power_management->frequency_value = (float)asic_frequency;
            }
            
            last_asic_frequency = asic_frequency;
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
