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
#include "esp_timer.h"

#define POLL_RATE 1800
#define MAX_TEMP 90.0
#define THROTTLE_TEMP 75.0
#define THROTTLE_TEMP_RANGE (MAX_TEMP - THROTTLE_TEMP)

#define VOLTAGE_START_THROTTLE 4900
#define VOLTAGE_MIN_THROTTLE 3500
#define VOLTAGE_RANGE (VOLTAGE_START_THROTTLE - VOLTAGE_MIN_THROTTLE)

#define TPS546_THROTTLE_TEMP 105.0
#define TPS546_MAX_TEMP 145.0

// Optimization constants
#define OPTIMIZATION_INTERVAL_MS 30000  // 30 seconds between optimizations
#define EFFICIENCY_HISTORY_SIZE 5
#define VOLTAGE_STEP_SIZE 0.005  // 5mV steps for voltage optimization
#define MIN_HASHRATE_THRESHOLD 0.5  // Minimum acceptable hashrate ratio

 master
static const char * TAG = "power_management";

// Efficiency history for voltage optimization
static float efficiency_history[EFFICIENCY_HISTORY_SIZE] = {0};
static int efficiency_index = 0;
static uint32_t last_optimization_time = 0;
static float last_optimization_voltage = 0;
static bool voltage_optimization_direction = true; // true = increase, false = decrease

// Set the fan speed between 20% min and 100% max based on chip temperature as input.
// The fan speed increases from 20% to 100% proportionally to the temperature increase from 50 and THROTTLE_TEMP
static double automatic_fan_speed(float chip_temp, GlobalState * GLOBAL_STATE)
{
    // Use the enhanced adaptive fan control if we're in thermal optimization mode
    if (GLOBAL_STATE->SYSTEM_MODULE.power_profile == POWER_PROFILE_EFFICIENCY || 
        GLOBAL_STATE->SYSTEM_MODULE.power_profile == POWER_PROFILE_DYNAMIC) {
        
        float target_temp = THROTTLE_TEMP - 5.0f; // Keep a 5°C buffer below throttle temp
        Thermal_adaptive_fan_control(GLOBAL_STATE, target_temp);
        return GLOBAL_STATE->POWER_MANAGEMENT_MODULE.fan_perc;
    }
    
    // Legacy fan control as fallback
    double result = 0.0;
    double min_temp = 45.0;
    double min_fan_speed = 35.0;

    if (chip_temp < min_temp) {
        result = min_fan_speed;
    } else if (chip_temp >= THROTTLE_TEMP) {
        result = 100;
    } else {
        double temp_range = THROTTLE_TEMP - min_temp;
        double fan_range = 100 - min_fan_speed;
        result = ((chip_temp - min_temp) / temp_range) * fan_range + min_fan_speed;
    }
    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    power_management->fan_perc = result;
    Thermal_set_fan_percent(GLOBAL_STATE->device_model, result/100.0);

	return result;
}

double pid_input = 0.0;
double pid_output = 0.0;
double pid_setPoint = 60.0;
double pid_p = 4.0;
double pid_i = 0.2;
double pid_d = 3.0;

PIDController pid;
 master

// Optimize voltage for efficiency
static void optimize_voltage_for_efficiency(GlobalState * GLOBAL_STATE) {
    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    uint32_t current_time = esp_timer_get_time() / 1000; // Convert to ms
    
    // Don't optimize too frequently
    if (current_time - last_optimization_time < OPTIMIZATION_INTERVAL_MS) {
        return;
    }
    
    // Calculate current efficiency
    float current_efficiency = Power_calculate_efficiency(GLOBAL_STATE);
    
    // Store in history
    efficiency_history[efficiency_index] = current_efficiency;
    efficiency_index = (efficiency_index + 1) % EFFICIENCY_HISTORY_SIZE;
    
    // Only optimize if we have some history and if dynamic voltage is enabled
    if (!power_management->dynamic_voltage || current_efficiency <= 0) {
        last_optimization_time = current_time;
        return;
    }
    
    // Get current voltage and determine whether previous optimization helped
    float current_voltage = power_management->voltage / 1000.0f;
    
    if (last_optimization_voltage > 0) {
        // Compare current efficiency with average of previous readings
        float avg_previous_efficiency = 0;
        int valid_readings = 0;
        
        for (int i = 0; i < EFFICIENCY_HISTORY_SIZE; i++) {
            if (efficiency_history[i] > 0) {
                avg_previous_efficiency += efficiency_history[i];
                valid_readings++;
            }
        }
        
        if (valid_readings > 0) {
            avg_previous_efficiency /= valid_readings;
            
            // If efficiency decreased significantly, reverse direction
            if (current_efficiency < avg_previous_efficiency * 0.98f) {
                voltage_optimization_direction = !voltage_optimization_direction;
                ESP_LOGI(TAG, "Efficiency decreased (%.2f vs %.2f), reversing voltage optimization direction", 
                         current_efficiency, avg_previous_efficiency);
            }
        }
    }
    
    // Adjust voltage in the current direction
    float new_voltage;
    if (voltage_optimization_direction) {
        // Try reducing voltage for better efficiency
        new_voltage = current_voltage - VOLTAGE_STEP_SIZE;
    } else {
        // Try increasing voltage for better stability
        new_voltage = current_voltage + VOLTAGE_STEP_SIZE;
    }
    
    // Ensure voltage is within safe range for current frequency
    float min_voltage = Power_calculate_min_voltage(GLOBAL_STATE, power_management->current_frequency);
    if (new_voltage < min_voltage) {
        new_voltage = min_voltage;
        voltage_optimization_direction = !voltage_optimization_direction; // Reverse direction for next time
    }
    
    // Apply new voltage
    ESP_LOGI(TAG, "Voltage optimization: %.3fV -> %.3fV (efficiency: %.2f)", current_voltage, new_voltage, current_efficiency);
    VCORE_set_voltage(new_voltage, GLOBAL_STATE);
    
    // Update tracking variables
    last_optimization_time = current_time;
    last_optimization_voltage = new_voltage;
    
    // Update NVS if optimization is significant
    if (fabs(new_voltage * 1000.0f - nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, CONFIG_ASIC_VOLTAGE)) > 10) {
        nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, (uint16_t)(new_voltage * 1000.0f));
    }
}

void POWER_MANAGEMENT_task(void * pvParameters)
{
    ESP_LOGI(TAG, "Starting");

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;
    
    // Initialize PID controller
    pid_setPoint = (double)nvs_config_get_u16(NVS_CONFIG_TEMP_TARGET, pid_setPoint);
    pid_init(&pid, &pid_input, &pid_output, &pid_setPoint, pid_p, pid_i, pid_d, PID_P_ON_E, PID_DIRECT);
    pid_set_sample_time(&pid, POLL_RATE - 1);
    pid_set_output_limits(&pid, 25, 100);
    pid_set_mode(&pid, AUTOMATIC);
    pid_set_controller_direction(&pid, PID_REVERSE);
    pid_initialize(&pid);

    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    SystemModule * sys_module = &GLOBAL_STATE->SYSTEM_MODULE;

    power_management->frequency_multiplier = 1;
    power_management->dynamic_voltage = true;
    power_management->voltage_offset = 0.0f;
    power_management->throttle_count = 0;
    
    // Initialize thermal threshold values
    GLOBAL_STATE->thermal_throttle_temp = DEFAULT_THERMAL_THROTTLE_TEMP;
    GLOBAL_STATE->thermal_shutdown_temp = DEFAULT_THERMAL_SHUTDOWN_TEMP;
    
    // Default to balanced power profile
    sys_module->power_profile = POWER_PROFILE_BALANCED;
    Power_apply_profile(GLOBAL_STATE, POWER_PROFILE_BALANCED);

    vTaskDelay(500 / portTICK_PERIOD_MS);
    uint16_t last_core_voltage = 0.0;
    uint16_t last_asic_frequency = power_management->frequency_value;
    
    while (1) {
 master
        // Update power measurements


        // Refresh PID setpoint from NVS in case it was changed via API
        pid_setPoint = (double)nvs_config_get_u16(NVS_CONFIG_TEMP_TARGET, pid_setPoint);

 master
        power_management->voltage = Power_get_input_voltage(GLOBAL_STATE);
        power_management->power = Power_get_power(GLOBAL_STATE);
        power_management->current = Power_get_current(GLOBAL_STATE);

        // Update thermal measurements
        power_management->fan_rpm = Thermal_get_fan_speed(GLOBAL_STATE->device_model);
        power_management->chip_temp_avg = Thermal_get_chip_temp(GLOBAL_STATE);
        power_management->vr_temp = Power_get_vreg_temp(GLOBAL_STATE);
        
        // Calculate power efficiency metrics
        power_management->power_efficiency = Power_calculate_efficiency(GLOBAL_STATE);
        
        // Get temperature trend
        TempTrend temp_trend = Thermal_get_temp_trend(GLOBAL_STATE);
        
        // Advanced thermal management
        if (temp_trend == TEMP_TREND_RISING && power_management->chip_temp_avg > (THROTTLE_TEMP - 5.0f)) {
            ESP_LOGW(TAG, "Temperature rising trend detected at %.2f°C, proactive cooling", power_management->chip_temp_avg);
            // Boost fan speed proactively
            power_management->fan_perc = 100;
            Thermal_set_fan_percent(GLOBAL_STATE->device_model, 1.0f);
        }

        // Overheat protection
        if ((power_management->vr_temp > TPS546_THROTTLE_TEMP || power_management->chip_temp_avg > THROTTLE_TEMP) && 
            (power_management->frequency_value > 50 || power_management->voltage > 1000)) {
            
            ESP_LOGE(TAG, "OVERHEAT! VR: %fC ASIC %fC", power_management->vr_temp, power_management->chip_temp_avg);
            
            power_management->throttle_count++;
            
            // Full fan speed
            power_management->fan_perc = 100;
            Thermal_set_fan_percent(GLOBAL_STATE->device_model, 1);

            // Severe overheating - emergency shutdown
            if (power_management->vr_temp > TPS546_MAX_TEMP || power_management->chip_temp_avg > MAX_TEMP || 
                power_management->throttle_count > 5) {
                
                ESP_LOGE(TAG, "CRITICAL TEMPERATURE EXCEEDED! Shutting down ASIC");
                
                // Turn off core voltage
                Power_disable(GLOBAL_STATE);

                // Save emergency settings
                nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, 1000);
                nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, 50);
                nvs_config_set_u16(NVS_CONFIG_FAN_SPEED, 100);
                nvs_config_set_u16(NVS_CONFIG_AUTO_FAN_SPEED, 1);
                nvs_config_set_u16(NVS_CONFIG_OVERHEAT_MODE, 1);
                
                exit(EXIT_FAILURE);
            } else {
                // Throttle down frequency by 10%
                uint16_t reduced_freq = (uint16_t)(power_management->frequency_value * 0.9f);
                
                if (reduced_freq < 50) reduced_freq = 50;
                
                ESP_LOGW(TAG, "Thermal throttling: Reducing frequency to %uMHz", reduced_freq);
                
                if (ASIC_set_frequency(GLOBAL_STATE, (float)reduced_freq)) {
                    power_management->frequency_value = (float)reduced_freq;
                    power_management->current_frequency = (float)reduced_freq;
                    nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, reduced_freq);
                }
            }
        } else {
            // Reset throttle counter if temperatures are back to normal
            if (power_management->vr_temp < (TPS546_THROTTLE_TEMP - 10) && 
                power_management->chip_temp_avg < (THROTTLE_TEMP - 10)) {
                power_management->throttle_count = 0;
            }
        }
 master

        // Fan speed control
        if (nvs_config_get_u16(NVS_CONFIG_AUTO_FAN_SPEED, 1) == 1) {
            power_management->fan_perc = (float)automatic_fan_speed(power_management->chip_temp_avg, GLOBAL_STATE);

        //enable the PID auto control for the FAN if set
        if (nvs_config_get_u16(NVS_CONFIG_AUTO_FAN_SPEED, 1) == 1) {
            // Ignore invalid temperature readings (-1) during startup
            if (power_management->chip_temp_avg >= 0) {
                pid_input = power_management->chip_temp_avg;
                pid_compute(&pid);
                power_management->fan_perc = (uint16_t) pid_output;
                Thermal_set_fan_percent(GLOBAL_STATE->device_model, pid_output / 100.0);
                ESP_LOGI(TAG, "Temp: %.1f°C, SetPoint: %.1f°C, Output: %.1f%%", pid_input, pid_setPoint, pid_output);
            } else {
                // Set fan to 70% in AP mode when temperature reading is invalid
                if (GLOBAL_STATE->SYSTEM_MODULE.ap_enabled) {
                    ESP_LOGW(TAG, "AP mode with invalid temperature reading: %.1f°C - Setting fan to 70%%", power_management->chip_temp_avg);
                    power_management->fan_perc = 70;
                    Thermal_set_fan_percent(GLOBAL_STATE->device_model, 0.7);
                } else {
                    ESP_LOGW(TAG, "Ignoring invalid temperature reading: %.1f°C", power_management->chip_temp_avg);
                }
            }
 master
        } else {
            float fs = (float) nvs_config_get_u16(NVS_CONFIG_FAN_SPEED, 100);
            power_management->fan_perc = fs;
            Thermal_set_fan_percent(GLOBAL_STATE->device_model, (float) fs / 100.0);
        }

        // Voltage and frequency adjustment
        uint16_t core_voltage = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, CONFIG_ASIC_VOLTAGE);
        uint16_t asic_frequency = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, CONFIG_ASIC_FREQUENCY);

        // Apply new voltage if changed
        if (core_voltage != last_core_voltage) {
            ESP_LOGI(TAG, "Setting new vcore voltage to %umV", core_voltage);
            VCORE_set_voltage((double) core_voltage / 1000.0, GLOBAL_STATE);
            last_core_voltage = core_voltage;
        }

        // Apply new frequency if changed
        if (asic_frequency != last_asic_frequency) {
            ESP_LOGI(TAG, "New ASIC frequency requested: %uMHz (current: %uMHz)", asic_frequency, last_asic_frequency);
            
            bool success = ASIC_set_frequency(GLOBAL_STATE, (float)asic_frequency);
            
            if (success) {
                power_management->frequency_value = (float)asic_frequency;
                power_management->current_frequency = (float)asic_frequency;
                
                // When frequency changes, optimize voltage for the new frequency
                if (power_management->dynamic_voltage) {
                    Power_optimize_voltage(GLOBAL_STATE);
                }
            }
            
            last_asic_frequency = asic_frequency;
        }

        // Check for profile change via NVS
        uint16_t profile_nvs = nvs_config_get_u16("power_profile", POWER_PROFILE_BALANCED);
        if (profile_nvs != sys_module->power_profile) {
            ESP_LOGI(TAG, "Changing power profile to %d", profile_nvs);
            Power_apply_profile(GLOBAL_STATE, (PowerProfile)profile_nvs);
        }

        // Check for changing of overheat mode
        uint16_t new_overheat_mode = nvs_config_get_u16(NVS_CONFIG_OVERHEAT_MODE, 0);
        if (new_overheat_mode != sys_module->overheat_mode) {
            sys_module->overheat_mode = new_overheat_mode;
            ESP_LOGI(TAG, "Overheat mode updated to: %d", sys_module->overheat_mode);
        }

        // Voltage optimization for efficiency (if enabled)
        if (power_management->dynamic_voltage && sys_module->power_profile != POWER_PROFILE_PERFORMANCE) {
            optimize_voltage_for_efficiency(GLOBAL_STATE);
        }

        VCORE_check_fault(GLOBAL_STATE);

        vTaskDelay(POLL_RATE / portTICK_PERIOD_MS);
    }
}
