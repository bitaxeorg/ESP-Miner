#include <string.h>
#include "EMC2101.h"
#include "INA260.h"
#include "bm1397.h"
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
#include "esp_timer.h"
#include "lvglDisplayBAP.h"
#include "dataBase.h"
#include "driver/gpio.h"
#include "common.h"
#include "system.h"
#include "esp_system.h"
#define GPIO_ASIC_ENABLE CONFIG_GPIO_ASIC_ENABLE
#define GPIO_ASIC_RESET  CONFIG_GPIO_ASIC_RESET
#define GPIO_PLUG_SENSE  CONFIG_GPIO_PLUG_SENSE

#define POLL_RATE 2000
#define MAX_TEMP 90.0
#define THROTTLE_TEMP 75.0
#define THROTTLE_TEMP_RANGE (MAX_TEMP - THROTTLE_TEMP)

#define VOLTAGE_START_THROTTLE 4900
#define VOLTAGE_MIN_THROTTLE 3500
#define VOLTAGE_RANGE (VOLTAGE_START_THROTTLE - VOLTAGE_MIN_THROTTLE)

#define TPS546_THROTTLE_TEMP 105.0
#define TPS546_MAX_TEMP 145.0

#define SUPRA_POWER_OFFSET 5
#define GAMMA_POWER_OFFSET 5

static const char * TAG = "power_management";



// Define preset arrays for each device model
// DEVICE_MAX presets
static const DevicePreset DEVICE_MAX_PRESETS[] = {
    {"quiet", 1100, 450, 50},       // Quiet: Low power, conservative
    {"balanced", 1200, 550, 65},    // Balanced: Good performance/efficiency balance
    {"turbo", 1400, 750, 100},      // Turbo: Maximum performance
};

// DEVICE_ULTRA presets
static const DevicePreset DEVICE_ULTRA_PRESETS[] = {
    {"quiet", 1130, 420, 25},       // Quiet: Low power, conservative
    {"balanced", 1190, 490,35},    // Balanced: Good performance/efficiency balance
    {"turbo", 1250, 625, 95},       // Turbo: Maximum performance
};

// DEVICE_SUPRA presets
static const DevicePreset DEVICE_SUPRA_PRESETS[] = {
    {"quiet", 1100, 425, 25},       // Quiet: Low power, conservative
    {"balanced", 1200, 575, 35},    // Balanced: Good performance/efficiency balance
    {"turbo", 1350, 750, 95},       // Turbo: Maximum performance
};

// DEVICE_GAMMA presets
static const DevicePreset DEVICE_GAMMA_PRESETS[] = {
    {"quiet", 1000, 400, 25},        // Quiet: Low power, conservative
    {"balanced", 1090, 490, 35},    // Balanced: Good performance/efficiency balance
    {"turbo", 1160, 600, 95},       // Turbo: Maximum performance
};

// Simple function to apply a preset by name
bool apply_preset(int device_model, const char* preset_name) {
    const DevicePreset* presets = NULL;
    uint8_t preset_count = 0;
    
    // Get the correct preset array for the device
    switch ((DeviceModel)device_model) {
        case DEVICE_MAX:
            presets = DEVICE_MAX_PRESETS;
            preset_count = sizeof(DEVICE_MAX_PRESETS) / sizeof(DevicePreset);
            break;
        case DEVICE_ULTRA:
            presets = DEVICE_ULTRA_PRESETS;
            preset_count = sizeof(DEVICE_ULTRA_PRESETS) / sizeof(DevicePreset);
            break;
        case DEVICE_SUPRA:
            presets = DEVICE_SUPRA_PRESETS;
            preset_count = sizeof(DEVICE_SUPRA_PRESETS) / sizeof(DevicePreset);
            break;
        case DEVICE_GAMMA:
            presets = DEVICE_GAMMA_PRESETS;
            preset_count = sizeof(DEVICE_GAMMA_PRESETS) / sizeof(DevicePreset);
            break;
        default:
            ESP_LOGI(TAG, "Unknown device model: %d", device_model);
            return false;
    }
    
    // Find the preset by name
    const DevicePreset* selected_preset = NULL;
    for (uint8_t i = 0; i < preset_count; i++) {
        if (strcmp(presets[i].name, preset_name) == 0) {
            selected_preset = &presets[i];
            break;
        }
    }
    
    if (selected_preset == NULL) {
        ESP_LOGI(TAG, "Invalid preset name '%s' for device model %d", preset_name, device_model);
        return false;
    }
    
    ESP_LOGI(TAG, "Applying preset \"%s\": %umV, %uMHz, %u%% fan", 
             selected_preset->name, selected_preset->domain_voltage_mv, 
             selected_preset->frequency_mhz, selected_preset->fan_speed_percent);
    
    // Set the values directly to NVS
    // set fan speed high for safety
    nvs_config_set_u16(NVS_CONFIG_FAN_SPEED, 100);
    nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, selected_preset->domain_voltage_mv);
    nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, selected_preset->frequency_mhz);
    nvs_config_set_u16(NVS_CONFIG_FAN_SPEED, selected_preset->fan_speed_percent);
    nvs_config_set_string(NVS_CONFIG_AUTOTUNE_PRESET, preset_name);
    nvs_config_set_u16(NVS_CONFIG_AUTO_FAN_SPEED, 0);
    nvs_config_set_u16(NVS_CONFIG_AUTOTUNE_FLAG, 1);


    // send through BAP
    //lvglSendPresetBAP();
    
    // Log preset change event
    char preset_data[256];
    snprintf(preset_data, sizeof(preset_data), 
             "{\"presetName\":\"%s\",\"voltage\":%u,\"frequency\":%u,\"fanSpeed\":%u,\"deviceModel\":%d}", 
             preset_name, selected_preset->domain_voltage_mv, selected_preset->frequency_mhz, 
             selected_preset->fan_speed_percent, device_model);
    dataBase_log_event("power", "info", "Preset configuration applied", preset_data);
    
    return true;
}

// autotune function
static void autotuneOffset(GlobalState * GLOBAL_STATE)
{
    // Access the autotune module
    AutotuneModule *autotune = &GLOBAL_STATE->AUTOTUNE_MODULE;
    PowerManagementModule *power = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    SystemModule *system = &GLOBAL_STATE->SYSTEM_MODULE;
    static const char *autotuneTAG = "autotune";

    // Check if autotune is enabled
    if (nvs_config_get_u16(NVS_CONFIG_AUTOTUNE_FLAG, 1) == 0) {
        ESP_LOGI(autotuneTAG, "Autotune is disabled");
        return;
    }

    // Get current global variables for autotune calculations
    uint16_t currentDomainVoltage = VCORE_get_voltage_mv(GLOBAL_STATE);  // Domain Voltage in mV
    uint16_t currentFrequency = (uint16_t)power->frequency_value;        // Frequency in MHz
    uint8_t currentAsicTemp = (uint8_t)power->chip_temp_avg;            // ASIC Temperature in °C
    uint8_t currentFanSpeed = (uint8_t)(power->fan_perc);               // Fan Speed in percentage
    float currentHashrate = system->current_hashrate;                   // Hashrate in GH/s
    int16_t currentPower = (int16_t)power->power;                       // Power in watts

    // Early return if temperature is invalid or hashrate is 0
    if (currentAsicTemp == 255) {
        ESP_LOGI(autotuneTAG, "Skipping autotune - Temperature sensor not initialized");
        return;
    }

    if (currentHashrate <= 0) {
        ESP_LOGI(autotuneTAG, "Skipping autotune - Hashrate is 0");
        return;
    }

    // Get target values from autotune module
    //int16_t targetPower = autotune->targetPower;                        // Target power in watts
    uint16_t targetDomainVoltage = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, CONFIG_ASIC_VOLTAGE);     // Target voltage in mV
    //uint16_t targetFrequency = autotune->targetFrequency;               // Target frequency in MHz
    //uint8_t targetFanSpeed = autotune->targetFanSpeed;                  // Target fan speed in percentage
    uint8_t targetAsicTemp = 60;            // Target temperature in °C
    float targetHashrate = currentFrequency * ((GLOBAL_STATE->small_core_count * GLOBAL_STATE->asic_count) / 1000.0);



    // Log current values
    ESP_LOGI(autotuneTAG, "Autotune - Current Values:");
    ESP_LOGI(autotuneTAG, "  Domain Voltage: %u mV", currentDomainVoltage);
    ESP_LOGI(autotuneTAG, "  Frequency: %u MHz", currentFrequency);
    ESP_LOGI(autotuneTAG, "  ASIC Temp: %u °C", currentAsicTemp);
    ESP_LOGI(autotuneTAG, "  Fan Speed: %u %%", currentFanSpeed);
    ESP_LOGI(autotuneTAG, "  Hashrate: %.2f GH/s", currentHashrate);
    ESP_LOGI(autotuneTAG, "  Power: %d W", currentPower);
    ESP_LOGI(autotuneTAG, "  Max Power: %d W", GLOBAL_STATE->AUTOTUNE_MODULE.maxPower);
    ESP_LOGI(autotuneTAG, "  Max Domain Voltage: %u mV", GLOBAL_STATE->AUTOTUNE_MODULE.maxDomainVoltage);
    ESP_LOGI(autotuneTAG, "  Max Frequency: %u MHz", GLOBAL_STATE->AUTOTUNE_MODULE.maxFrequency);
    ESP_LOGI(autotuneTAG, "  Min Domain Voltage: %u mV", GLOBAL_STATE->AUTOTUNE_MODULE.minDomainVoltage);
    ESP_LOGI(autotuneTAG, "  Min Frequency: %u MHz", GLOBAL_STATE->AUTOTUNE_MODULE.minFrequency);

    // Log target values
    ESP_LOGI(autotuneTAG, "Autotune - Target Values:");
    //ESP_LOGI(autotuneTAG, "  Target Power: %d W", targetPower);
    ESP_LOGI(autotuneTAG, "  Target Domain Voltage: %u mV", targetDomainVoltage);
    //ESP_LOGI(autotuneTAG, "  Target Frequency: %u MHz", targetFrequency);
    //ESP_LOGI(autotuneTAG, "  Target Fan Speed: %u %%", targetFanSpeed);
    ESP_LOGI(autotuneTAG, "  Target Temperature: %u °C", targetAsicTemp);
    ESP_LOGI(autotuneTAG, "  Target Hashrate: %.2f GH/s", targetHashrate);

    
    // Timing mechanism for normal operation
    static TickType_t lastAutotuneTime = 0;
    TickType_t currentTime = xTaskGetTickCount();
    uint32_t uptimeSeconds = (esp_timer_get_time() - GLOBAL_STATE->SYSTEM_MODULE.start_time) / 1000000;
    
    // Check if we need to wait for initial warmup (15 minutes)
    if (uptimeSeconds < 900 && currentAsicTemp < targetAsicTemp) { // 15 minutes = 900 seconds
        ESP_LOGI(autotuneTAG, "Autotune - Waiting for initial warmup period (%lu seconds remaining)", 900 - uptimeSeconds);
        return;
    }
    
    // Determine timing interval based on temperature
    uint32_t intervalMs;
    if (currentAsicTemp < 68) {
        intervalMs = 300000; // 5 minutes for normal operation
    } else {
        intervalMs = 500;  // 5 seconds for higher temperatures
    }
    
    // Check if enough time has passed since last autotune
    if ((currentTime - lastAutotuneTime) < pdMS_TO_TICKS(intervalMs)) {
        ESP_LOGI(autotuneTAG, "Autotune - Waiting for next adjustment interval (%lu ms remaining)", 
                 intervalMs - ((currentTime - lastAutotuneTime) * portTICK_PERIOD_MS));
        return;
    }
    
    // Update last autotune time
    lastAutotuneTime = currentTime;
    
    // Safety mechanism: Track consecutive low hashrate attempts
    static uint8_t consecutiveLowHashrateAttempts = 0;
    float hashrateThreshold = targetHashrate * 0.5; // 50% of target hashrate
    
    if (currentHashrate < hashrateThreshold) {
        consecutiveLowHashrateAttempts++;
        ESP_LOGI(autotuneTAG, "Low hashrate detected: %.2f GH/s (threshold: %.2f GH/s), consecutive attempts: %u", 
                 currentHashrate, hashrateThreshold, consecutiveLowHashrateAttempts);
        char data[128];
        snprintf(data, sizeof(data), "{\"currentHashrate\":%.2f,\"hashrateThreshold\":%.2f,\"consecutiveAttempts\":%u}", 
                 currentHashrate, hashrateThreshold, consecutiveLowHashrateAttempts);
        dataBase_log_event("power", "warn", "Autotune - Low hashrate detected", data);
        
        // If we've had 3 consecutive low hashrate attempts, reapply preset
        if (consecutiveLowHashrateAttempts >= 3) {
            static char current_preset[32];
            char* preset_ptr = nvs_config_get_string(NVS_CONFIG_AUTOTUNE_PRESET, "balanced");
            strncpy(current_preset, preset_ptr, sizeof(current_preset) - 1);
            current_preset[sizeof(current_preset) - 1] = '\0';
            free(preset_ptr);
            
            ESP_LOGE(autotuneTAG, "SAFETY: 3 consecutive low hashrate attempts detected, reapplying preset '%s'", current_preset);
            
            // Log safety reset event
            char safety_data[256];
            snprintf(safety_data, sizeof(safety_data), 
                     "{\"consecutiveAttempts\":%u,\"currentHashrate\":%.2f,\"targetHashrate\":%.2f,\"threshold\":%.2f,\"preset\":\"%s\"}", 
                     consecutiveLowHashrateAttempts, currentHashrate, targetHashrate, hashrateThreshold, current_preset);
            dataBase_log_event("power", "critical", "Autotune safety reset - consecutive low hashrate attempts", safety_data);
            
            // Reapply the current preset
            if (apply_preset(GLOBAL_STATE->device_model, current_preset)) {
                ESP_LOGI(autotuneTAG, "Successfully reapplied preset '%s'", current_preset);
            } else {
                ESP_LOGE(autotuneTAG, "Failed to reapply preset '%s'", current_preset);
            }
            
            // Reset the counter
            consecutiveLowHashrateAttempts = 0;
            return;
        }
    } else {
        // Hashrate is good, reset the counter
        if (consecutiveLowHashrateAttempts > 0) {
            ESP_LOGI(autotuneTAG, "Hashrate recovered: %.2f GH/s, resetting consecutive low hashrate counter", currentHashrate);
            consecutiveLowHashrateAttempts = 0;
        }
    }
    
    // Temperature-based adjustments
    int8_t tempDiff = currentAsicTemp - targetAsicTemp;
    
    // If temperature is within 2 degrees of target
    if (tempDiff >= -2 && tempDiff <= 2) {
        // Check hashrate
        float hashrateDiffPercent = ((currentHashrate - targetHashrate) / targetHashrate) * 100.0;
        
        if (hashrateDiffPercent < -20.0) { // Hashrate is below target by 20%
            // Increase voltage by 10mV
            uint16_t newVoltage = targetDomainVoltage + 10;
            ESP_LOGI(autotuneTAG, "Autotune - Increasing voltage from %u mV to %u mV", targetDomainVoltage, newVoltage);
            char data[128];
            snprintf(data, sizeof(data), "{\"voltage\":%u, \"frequency\":%u, \"temperature\":%u, \"hashrate\":%.2f, \"targetHashrate\":%.2f, }", 
            newVoltage, currentFrequency, currentAsicTemp, currentHashrate, targetHashrate);
            dataBase_log_event("power", "info", "Autotune - Hashrate below target, increasing voltage", data);
            nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, newVoltage);
            return;
        } else {
            ESP_LOGI(TAG, "Autotune - Hashrate above target, no adjustments needed");
            char data[128];
            snprintf(data, sizeof(data), "{\"voltage\":%u, \"frequency\":%u, \"temperature\":%u, \"hashrate\":%.2f, \"targetHashrate\":%.2f, }", 
            targetDomainVoltage, currentFrequency, currentAsicTemp, currentHashrate, targetHashrate);
            dataBase_log_event("power", "info", "Autotune - Hashrate above target, no adjustments needed", data);
            return;
        }
    }
    // If temperature is under target
    else if (tempDiff < -2) {
        static uint16_t newFrequency = 0;
        static uint16_t newVoltage = 0;
        // Increase frequency by 2%
        if (currentFrequency < GLOBAL_STATE->AUTOTUNE_MODULE.maxFrequency && GLOBAL_STATE->AUTOTUNE_MODULE.maxPower > currentPower) {
           newFrequency = currentFrequency * 1.02;
        }
        else {
            ESP_LOGI(TAG, "freq or power limit reached, no adjustments possible");
            ESP_LOGI(TAG, "Autotune - Frequency: %u MHz, Power: %d W, Max Frequency: %u MHz, Max Power: %d W", 
                     currentFrequency, currentPower, GLOBAL_STATE->AUTOTUNE_MODULE.maxFrequency, GLOBAL_STATE->AUTOTUNE_MODULE.maxPower);
            char data[128];
            snprintf(data, sizeof(data), "{\"frequency\":%u,\"power\":%d,\"maxFrequency\":%u,\"maxPower\":%d}", 
                     currentFrequency, currentPower, GLOBAL_STATE->AUTOTUNE_MODULE.maxFrequency, GLOBAL_STATE->AUTOTUNE_MODULE.maxPower);
            dataBase_log_event("power", "warn", "Autotune - Frequency or power limit reached, no adjustments possible", data);

            return;
        }
        // Increase voltage by 0.2%
        if (targetDomainVoltage < GLOBAL_STATE->AUTOTUNE_MODULE.maxDomainVoltage && GLOBAL_STATE->AUTOTUNE_MODULE.maxPower > currentPower) {
            newVoltage = targetDomainVoltage * 1.002;
        }
        else {
            ESP_LOGI(TAG, "voltage or power limit reached, no adjustments possible");
            ESP_LOGI(TAG, "Autotune - Voltage: %u mV, Power: %d W, Max Voltage: %u mV, Max Power: %d W", 
                     targetDomainVoltage, currentPower, GLOBAL_STATE->AUTOTUNE_MODULE.maxDomainVoltage, GLOBAL_STATE->AUTOTUNE_MODULE.maxPower);
            return;
        }
        
        ESP_LOGI(TAG, "Autotune - Temperature under target, increasing frequency from %u MHz to %u MHz", 
                 currentFrequency, newFrequency);
        ESP_LOGI(TAG, "Autotune - Increasing voltage from %u mV to %u mV", targetDomainVoltage, newVoltage);
        char data[128];
        snprintf(data, sizeof(data), "{\"newFrequency\":%u,\"newVoltage\":%u, \"currentTemperature\":%u, \"currentHashrate\":%.2f, \"targetHashrate\":%.2f}", 
        newFrequency, newVoltage, currentAsicTemp, currentHashrate, targetHashrate);
        dataBase_log_event("power", "info", "Autotune - Temperature under target, increasing frequency and voltage", data);
        
        nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, newFrequency);
        nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, newVoltage);
        return;
    }
    // If temperature is over target
    else {
        // Decrease frequency by 2%
        uint16_t newFrequency = currentFrequency * 0.98;
        // Ensure frequency doesn't go below minimum
        if (newFrequency < GLOBAL_STATE->AUTOTUNE_MODULE.minFrequency) {
            newFrequency = GLOBAL_STATE->AUTOTUNE_MODULE.minFrequency;
            ESP_LOGI(TAG, "Autotune - Frequency limited to minimum: %u MHz", newFrequency);
        }
        
        // Decrease voltage by 0.2%
        uint16_t newVoltage = targetDomainVoltage * 0.998;
        // Ensure voltage doesn't go below minimum
        if (newVoltage < GLOBAL_STATE->AUTOTUNE_MODULE.minDomainVoltage) {
            newVoltage = GLOBAL_STATE->AUTOTUNE_MODULE.minDomainVoltage;
            ESP_LOGI(TAG, "Autotune - Voltage limited to minimum: %u mV", newVoltage);
        }
        
        // Only apply changes if they're different from current values
        bool frequencyChanged = (newFrequency != currentFrequency);
        bool voltageChanged = (newVoltage != targetDomainVoltage);
        
        if (!frequencyChanged && !voltageChanged) {
            ESP_LOGI(TAG, "Autotune - At minimum limits, no further adjustments possible");
            char data[128];
            snprintf(data, sizeof(data), "{\"currentFrequency\":%u,\"currentVoltage\":%u,\"minFrequency\":%u,\"minVoltage\":%u,\"currentTemperature\":%u}", 
                     currentFrequency, targetDomainVoltage, GLOBAL_STATE->AUTOTUNE_MODULE.minFrequency, 
                     GLOBAL_STATE->AUTOTUNE_MODULE.minDomainVoltage, currentAsicTemp);
            dataBase_log_event("power", "warn", "Autotune - At minimum limits, no further adjustments possible", data);
            return;
        }
        
        if (frequencyChanged) {
            ESP_LOGI(TAG, "Autotune - Temperature over target, decreasing frequency from %u MHz to %u MHz", 
                     currentFrequency, newFrequency);
            nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, newFrequency);
        }
        
        if (voltageChanged) {
            ESP_LOGI(TAG, "Autotune - Decreasing voltage from %u mV to %u mV", targetDomainVoltage, newVoltage);
            char data[128];
            nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, newVoltage);
        }
        
        char data[128];
        snprintf(data, sizeof(data), "{\"newFrequency\":%u,\"newVoltage\":%u,\"currentTemperature\":%u,\"currentHashrate\":%.2f,\"targetHashrate\":%.2f}", 
                 newFrequency, newVoltage, currentAsicTemp, currentHashrate, targetHashrate);
        dataBase_log_event("power", "info", "Autotune - Temperature over target, decreasing frequency and/or voltage", data);
        
        return;
    }
}

// static float _fbound(float value, float lower_bound, float upper_bound)
// {
//     if (value < lower_bound)
//         return lower_bound;
//     if (value > upper_bound)
//         return upper_bound;

//     return value;
// }

// Set the fan speed between 20% min and 100% max based on chip temperature as input.
// The fan speed increases from 20% to 100% proportionally to the temperature increase from 50 and THROTTLE_TEMP
static double automatic_fan_speed(float chip_temp, GlobalState * GLOBAL_STATE)
{
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

    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
        case DEVICE_GAMMA:
            float perc = (float) result / 100;
            GLOBAL_STATE->POWER_MANAGEMENT_MODULE.fan_perc = perc;
            EMC2101_set_fan_speed( perc );
            break;
        default:
    }
	return result;
}

// Soft overheat recovery function
static void handle_overheat_recovery(GlobalState * GLOBAL_STATE, const char* device_info, const char* log_data) {
    ESP_LOGE(TAG, "OVERHEAT DETECTED: %s", device_info);
    
    // Set fan to full speed and turn off VCORE (immediate safety measures)
    EMC2101_set_fan_speed(1);
    
    // Turn off VCORE based on device type
    PowerManagementModule *power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
            if (power_management->HAS_POWER_EN) {
                gpio_set_level(GPIO_ASIC_ENABLE, 1);
            }
            break;
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
            if (GLOBAL_STATE->board_version >= 402 && GLOBAL_STATE->board_version <= 499) {
                VCORE_set_voltage(0.0, GLOBAL_STATE);
            } else if (power_management->HAS_POWER_EN) {
                gpio_set_level(GPIO_ASIC_ENABLE, 1);
            }
            break;
        case DEVICE_GAMMA:
            VCORE_set_voltage(0.0, GLOBAL_STATE);
            break;
        default:
            break;
    }
    
    // Set safe NVS values and enable overheat mode
    nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, 1000);
    nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, 50);
    nvs_config_set_u16(NVS_CONFIG_FAN_SPEED, 100);
    nvs_config_set_u16(NVS_CONFIG_AUTO_FAN_SPEED, 0);
    nvs_config_set_u16(NVS_CONFIG_OVERHEAT_MODE, 1);
    
    // Log the overheat event
    dataBase_log_event("power", "critical", "Overheat mode activated - temperature exceeded threshold", log_data);
    
    ESP_LOGE(TAG, "Entering overheat recovery mode. Waiting 30 seconds before automatic recovery...");
    
    // Wait 5 minutes for cooling
    vTaskDelay(300000 / portTICK_PERIOD_MS);
    
    ESP_LOGI(TAG, "Overheat recovery: Applying balanced preset and restarting...");
    
    // Reset overheat mode and apply balanced preset
    
    
    // Apply balanced preset for safe recovery
    if (apply_preset(GLOBAL_STATE->device_model, "balanced")) {
        ESP_LOGI(TAG, "Successfully applied balanced preset for recovery");
    } else {
        ESP_LOGE(TAG, "Failed to apply balanced preset, using safe defaults");
        nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, 1100);
        nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, 400);
        nvs_config_set_u16(NVS_CONFIG_FAN_SPEED, 75);
        nvs_config_set_u16(NVS_CONFIG_AUTO_FAN_SPEED, 1);
    }
    
    nvs_config_set_u16(NVS_CONFIG_OVERHEAT_MODE, 0);
    // Log recovery event
    dataBase_log_event("power", "info", "Overheat recovery completed - restarting system", "{}");
    
    // Restart the ESP32
    esp_restart();
}

void POWER_MANAGEMENT_task(void * pvParameters)
{
    ESP_LOGI(TAG, "Starting");

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;

    power_management->frequency_multiplier = 1;

    power_management->HAS_POWER_EN = GLOBAL_STATE->board_version == 202 || GLOBAL_STATE->board_version == 203 || GLOBAL_STATE->board_version == 204;
    power_management->HAS_PLUG_SENSE = GLOBAL_STATE->board_version == 204;

    //int last_frequency_increase = 0;
    //uint16_t frequency_target = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, CONFIG_ASIC_FREQUENCY);

    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
			if (GLOBAL_STATE->board_version < 402 || GLOBAL_STATE->board_version > 499) {
                // Configure plug sense pin as input(barrel jack) 1 is plugged in
                gpio_config_t barrel_jack_conf = {
                    .pin_bit_mask = (1ULL << GPIO_PLUG_SENSE),
                    .mode = GPIO_MODE_INPUT,
                };
                gpio_config(&barrel_jack_conf);
                int barrel_jack_plugged_in = gpio_get_level(GPIO_PLUG_SENSE);

                gpio_set_direction(GPIO_ASIC_ENABLE, GPIO_MODE_OUTPUT);
                if (barrel_jack_plugged_in == 1 || !power_management->HAS_PLUG_SENSE) {
                    // turn ASIC on
                    gpio_set_level(GPIO_ASIC_ENABLE, 0);
                } else {
                    // turn ASIC off
                    gpio_set_level(GPIO_ASIC_ENABLE, 1);
                }
			}
            break;
        case DEVICE_GAMMA:
            break;
        default:
    }

    vTaskDelay(500 / portTICK_PERIOD_MS);
    uint16_t last_core_voltage = 0.0;
    uint16_t last_asic_frequency = power_management->frequency_value;
    
    while (1) {

        switch (GLOBAL_STATE->device_model) {
            case DEVICE_MAX:
            case DEVICE_ULTRA:
            case DEVICE_SUPRA:
				if (GLOBAL_STATE->board_version >= 402 && GLOBAL_STATE->board_version <= 499) {
                    power_management->voltage = TPS546_get_vin() * 1000;
                    power_management->current = TPS546_get_iout() * 1000;
                    // calculate regulator power (in milliwatts)
                    power_management->power = (TPS546_get_vout() * power_management->current) / 1000;
                    // The power reading from the TPS546 is only it's output power. So the rest of the Bitaxe power is not accounted for.
                    power_management->power += SUPRA_POWER_OFFSET; // Add offset for the rest of the Bitaxe power. TODO: this better.
				} else {
                    if (INA260_installed() == true) {
                        power_management->voltage = INA260_read_voltage();
                        power_management->current = INA260_read_current();
                        power_management->power = INA260_read_power() / 1000;
                    }
				}
            
                break;
            case DEVICE_GAMMA:
                    power_management->voltage = TPS546_get_vin() * 1000;
                    power_management->current = TPS546_get_iout() * 1000;
                    // calculate regulator power (in milliwatts)
                    power_management->power = (TPS546_get_vout() * power_management->current) / 1000;
                    // The power reading from the TPS546 is only it's output power. So the rest of the Bitaxe power is not accounted for.
                    power_management->power += GAMMA_POWER_OFFSET; // Add offset for the rest of the Bitaxe power. TODO: this better.
                break;
            default:
        }

        power_management->fan_rpm = EMC2101_get_fan_speed();

        switch (GLOBAL_STATE->device_model) {
            case DEVICE_MAX:
                power_management->chip_temp_avg = GLOBAL_STATE->ASIC_initalized ? EMC2101_get_external_temp() : -1;

                if ((power_management->chip_temp_avg > THROTTLE_TEMP) &&
                    (power_management->frequency_value > 50 || power_management->voltage > 1000)) {
                    
                    // Prepare log data
                    char overheat_data[128];
                    snprintf(overheat_data, sizeof(overheat_data), 
                             "{\"chipTemp\":%.1f,\"threshold\":%f,\"device\":\"DEVICE_MAX\"}", 
                             power_management->chip_temp_avg, THROTTLE_TEMP);
                    
                    // Use soft overheat recovery
                    char device_info[64];
                    snprintf(device_info, sizeof(device_info), "DEVICE_MAX ASIC %.1fC", power_management->chip_temp_avg);
                    handle_overheat_recovery(GLOBAL_STATE, device_info, overheat_data);
                }
                break;
            case DEVICE_ULTRA:
            case DEVICE_SUPRA:
                
                if (GLOBAL_STATE->board_version >= 402 && GLOBAL_STATE->board_version <= 499) {
                    power_management->chip_temp_avg = GLOBAL_STATE->ASIC_initalized ? EMC2101_get_external_temp() : -1;
                    power_management->vr_temp = (float)TPS546_get_temperature();
                } else {
                    power_management->chip_temp_avg = EMC2101_get_internal_temp() + 5;
                    power_management->vr_temp = 0.0;
                }

                // EMC2101 will give bad readings if the ASIC is turned off
                if(power_management->voltage < TPS546_INIT_VOUT_MIN){
                    break;
                }

                //overheat mode if the voltage regulator or ASIC is too hot
                if ((power_management->vr_temp > TPS546_THROTTLE_TEMP || power_management->chip_temp_avg > THROTTLE_TEMP) &&
                    (power_management->frequency_value > 50 || power_management->voltage > 1000)) {
                    
                    // Prepare log data
                    char overheat_data[128];
                    snprintf(overheat_data, sizeof(overheat_data), 
                             "{\"vrTemp\":%.1f,\"chipTemp\":%.1f,\"vrThreshold\":%f,\"chipThreshold\":%f,\"device\":\"DEVICE_ULTRA_SUPRA\"}", 
                             power_management->vr_temp, power_management->chip_temp_avg, TPS546_THROTTLE_TEMP, THROTTLE_TEMP);
                    
                    // Use soft overheat recovery
                    char device_info[64];
                    snprintf(device_info, sizeof(device_info), "DEVICE_ULTRA/SUPRA VR: %.1fC ASIC %.1fC", 
                             power_management->vr_temp, power_management->chip_temp_avg);
                    handle_overheat_recovery(GLOBAL_STATE, device_info, overheat_data);
                }

                break;
            case DEVICE_GAMMA:
                power_management->chip_temp_avg = GLOBAL_STATE->ASIC_initalized ? EMC2101_get_external_temp() : -1;
                power_management->vr_temp = (float)TPS546_get_temperature();

                // EMC2101 will give bad readings if the ASIC is turned off
                if(power_management->voltage < TPS546_INIT_VOUT_MIN){
                    break;
                }

                //overheat mode if the voltage regulator or ASIC is too hot
                if ((power_management->vr_temp > TPS546_THROTTLE_TEMP || power_management->chip_temp_avg > THROTTLE_TEMP) &&
                    (power_management->frequency_value > 50 || power_management->voltage > 1000)) {
                    
                    // Prepare log data
                    char overheat_data[128];
                    snprintf(overheat_data, sizeof(overheat_data), 
                             "{\"vrTemp\":%.1f,\"chipTemp\":%.1f,\"vrThreshold\":%f,\"chipThreshold\":%f,\"device\":\"DEVICE_GAMMA\"}", 
                             power_management->vr_temp, power_management->chip_temp_avg, TPS546_THROTTLE_TEMP, THROTTLE_TEMP);
                    
                    // Use soft overheat recovery
                    char device_info[64];
                    snprintf(device_info, sizeof(device_info), "DEVICE_GAMMA VR: %.1fC ASIC %.1fC", 
                             power_management->vr_temp, power_management->chip_temp_avg);
                    handle_overheat_recovery(GLOBAL_STATE, device_info, overheat_data);
                }
                break;
            default:
        }


        if (nvs_config_get_u16(NVS_CONFIG_AUTO_FAN_SPEED, 1) == 1) {

            power_management->fan_perc = (float)automatic_fan_speed(power_management->chip_temp_avg, GLOBAL_STATE);

        } else {
            switch (GLOBAL_STATE->device_model) {
                case DEVICE_MAX:
                case DEVICE_ULTRA:
                case DEVICE_SUPRA:
                case DEVICE_GAMMA:

                    float fs = (float) nvs_config_get_u16(NVS_CONFIG_FAN_SPEED, 100);
                    power_management->fan_perc = fs;
                    EMC2101_set_fan_speed((float) fs / 100);

                    break;
                default:
            }
        }

        // Read the state of plug sense pin
        if (power_management->HAS_PLUG_SENSE) {
            int gpio_plug_sense_state = gpio_get_level(GPIO_PLUG_SENSE);
            if (gpio_plug_sense_state == 0) {
                // turn ASIC off
                gpio_set_level(GPIO_ASIC_ENABLE, 1);
            }
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
            if (do_frequency_transition((float)asic_frequency)) {
                power_management->frequency_value = (float)asic_frequency;
                ESP_LOGI(TAG, "Successfully transitioned to new ASIC frequency: %uMHz", asic_frequency);
            } else {
                ESP_LOGE(TAG, "Failed to transition to new ASIC frequency: %uMHz", asic_frequency);
            }
            last_asic_frequency = asic_frequency;
        }

        // Check for changing of overheat mode
        SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;
        uint16_t new_overheat_mode = nvs_config_get_u16(NVS_CONFIG_OVERHEAT_MODE, 0);
        
        if (new_overheat_mode != module->overheat_mode) {
            module->overheat_mode = new_overheat_mode;
            ESP_LOGI(TAG, "Overheat mode updated to: %d", module->overheat_mode);
        }
        autotuneOffset(GLOBAL_STATE);
        vTaskDelay(POLL_RATE / portTICK_PERIOD_MS);
    }
}
