#include "thermal.h"
#include "esp_log.h"
#include <math.h>

#define INTERNAL_OFFSET 5 //degrees C

// Fan control parameters
#define FAN_MIN_TEMP 45.0f         // Below this temperature, fan runs at minimum speed
#define FAN_MAX_TEMP 75.0f         // Above this temperature, fan runs at maximum speed
#define FAN_TEMP_BUFFER 3.0f       // Temperature buffer to prevent oscillation
#define FAN_MIN_SPEED 0.15f        // Minimum fan speed (15%)
#define FAN_CURVE_EXPONENTIAL 2.0f // Exponent for fan curve (higher = more aggressive at high temps)

// Temperature prediction parameters
#define TEMP_HISTORY_SIZE 10
#define TEMP_PREDICTION_THRESHOLD 0.5f  // °C/s - rate of temperature increase that triggers proactive cooling

static const char *TAG = "thermal";
static float temp_history[TEMP_HISTORY_SIZE] = {0};
static int temp_history_index = 0;
static int64_t last_temp_time = 0;

 master
esp_err_t Thermal_init(DeviceModel device_model, bool polarity) {
    //init the EMC2101, if we have one

esp_err_t Thermal_init(DeviceModel device_model) {
        //init the EMC2101, if we have one
 master
    switch (device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
            EMC2101_init();
            break;
        case DEVICE_GAMMA:
            EMC2101_init();
            EMC2101_set_ideality_factor(EMC2101_IDEALITY_1_0319);
            EMC2101_set_beta_compensation(EMC2101_BETA_11);
            break;
        case DEVICE_GAMMATURBO:
            EMC2103_init();
            break;
        default:
    }
    
    // Initialize temperature history
    for (int i = 0; i < TEMP_HISTORY_SIZE; i++) {
        temp_history[i] = 0;
    }
    temp_history_index = 0;
    last_temp_time = esp_timer_get_time();
    
    ESP_LOGI(TAG, "Thermal management initialized");
    return ESP_OK;
}

// Calculate fan speed based on temperature using a non-linear curve
float Thermal_calculate_fan_speed(float current_temp, float target_temp) {
    // If temperature is below minimum threshold, use minimum fan speed
    if (current_temp < FAN_MIN_TEMP) {
        return FAN_MIN_SPEED;
    }
    
    // If temperature is above maximum threshold, use maximum fan speed
    if (current_temp > FAN_MAX_TEMP) {
        return 1.0f;
    }
    
    // Calculate normalized temperature position between min and max thresholds
    float temp_range = FAN_MAX_TEMP - FAN_MIN_TEMP;
    float normalized_temp = (current_temp - FAN_MIN_TEMP) / temp_range;
    
    // Apply non-linear curve for more aggressive cooling at higher temperatures
    float fan_speed = powf(normalized_temp, FAN_CURVE_EXPONENTIAL);
    
    // Scale fan speed between min and max values
    fan_speed = FAN_MIN_SPEED + ((1.0f - FAN_MIN_SPEED) * fan_speed);
    
    // If we're approaching target temperature, increase fan speed proactively
    if (current_temp > (target_temp - FAN_TEMP_BUFFER)) {
        fan_speed = fmaxf(fan_speed, 0.7f); // Minimum 70% when approaching target
    }
    
    // Ensure fan speed is within valid range
    if (fan_speed > 1.0f) fan_speed = 1.0f;
    if (fan_speed < FAN_MIN_SPEED) fan_speed = FAN_MIN_SPEED;
    
    return fan_speed;
}

// Add temperature to history and predict future temperature
float Thermal_predict_temperature(float current_temp) {
    int64_t current_time = esp_timer_get_time();
    float time_diff = (current_time - last_temp_time) / 1000000.0f; // Convert to seconds
    
    // Store current temperature in history
    temp_history[temp_history_index] = current_temp;
    temp_history_index = (temp_history_index + 1) % TEMP_HISTORY_SIZE;
    
    // Only predict if we have enough time difference to avoid noise
    if (time_diff < 0.1f) {
        return current_temp; // Not enough time has passed
    }
    
    // Calculate temperature change rate (°C/second)
    float temp_rate = 0.0f;
    if (time_diff > 0) {
        // Use oldest temperature in history for calculation
        int oldest_index = (temp_history_index + 1) % TEMP_HISTORY_SIZE;
        float oldest_temp = temp_history[oldest_index];
        
        // Only calculate if we have valid history
        if (oldest_temp > 0) {
            temp_rate = (current_temp - oldest_temp) / time_diff;
        }
    }
    
    // Predict temperature in next 5 seconds
    float predicted_temp = current_temp + (temp_rate * 5.0f);
    
    // Update last temperature time
    last_temp_time = current_time;
    
    // Log if temperature is rising rapidly
    if (temp_rate > TEMP_PREDICTION_THRESHOLD) {
        ESP_LOGW(TAG, "Temperature rising at %.2f°C/s, predicted: %.2f°C", temp_rate, predicted_temp);
    }
    
    return predicted_temp;
}

//percent is a float between 0.0 and 1.0
esp_err_t Thermal_set_fan_percent(DeviceModel device_model, float percent) {
    // Ensure percent is within valid range
    if (percent < 0.0f) percent = 0.0f;
    if (percent > 1.0f) percent = 1.0f;
    
    ESP_LOGD(TAG, "Setting fan speed to %.1f%%", percent * 100.0f);

    switch (device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
        case DEVICE_GAMMA:
            EMC2101_set_fan_speed(percent);
            break;
        case DEVICE_GAMMATURBO:
            EMC2103_set_fan_speed(percent);
            break;
        default:
    }
    return ESP_OK;
}

// Optimized adaptive fan control
esp_err_t Thermal_adaptive_fan_control(GlobalState * GLOBAL_STATE, float target_temp) {
    if (!GLOBAL_STATE->SYSTEM_MODULE.autofanspeed) {
        return ESP_OK; // Skip if auto fan control is disabled
    }

    float current_temp = Thermal_get_chip_temp(GLOBAL_STATE);
    
    // Predict future temperature based on current trend
    float predicted_temp = Thermal_predict_temperature(current_temp);
    
    // Use the higher of current or predicted temperature for fan control
    float control_temp = (predicted_temp > current_temp) ? predicted_temp : current_temp;
    
    // Calculate optimal fan speed
    float fan_speed = Thermal_calculate_fan_speed(control_temp, target_temp);
    
    // Apply the fan speed
    Thermal_set_fan_percent(GLOBAL_STATE->device_model, fan_speed);
    
    // Store current fan percentage
    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.fan_perc = (uint16_t)(fan_speed * 100.0f);
    
    return ESP_OK;
}

uint16_t Thermal_get_fan_speed(DeviceModel device_model) {
    switch (device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
        case DEVICE_GAMMA:
            return EMC2101_get_fan_speed();
        case DEVICE_GAMMATURBO:
            return EMC2103_get_fan_speed();
        default:
    }
    return 0;
}

float Thermal_get_chip_temp(GlobalState * GLOBAL_STATE) {
    if (!GLOBAL_STATE->ASIC_initalized) {
        return -1;
    }

    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
            return EMC2101_get_external_temp();
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
            if (GLOBAL_STATE->board_version >= 402 && GLOBAL_STATE->board_version <= 499) {
                return EMC2101_get_external_temp();
            } else {
                return EMC2101_get_internal_temp() + INTERNAL_OFFSET;
            }
        case DEVICE_GAMMA:
            return EMC2101_get_external_temp();
        case DEVICE_GAMMATURBO:
            return EMC2103_get_external_temp();
        default:
    }

    return -1;
}

// Get temperature trend (rising, falling, stable)
TempTrend Thermal_get_temp_trend(GlobalState * GLOBAL_STATE) {
    int64_t current_time = esp_timer_get_time();
    float time_diff = (current_time - last_temp_time) / 1000000.0f; // Convert to seconds
    
    // Need enough time to have passed to determine trend
    if (time_diff < 1.0f) {
        return TEMP_TREND_STABLE;
    }
    
    float current_temp = Thermal_get_chip_temp(GLOBAL_STATE);
    
    // Use oldest temperature in history for calculation
    int oldest_index = (temp_history_index + 1) % TEMP_HISTORY_SIZE;
    float oldest_temp = temp_history[oldest_index];
    
    // Only calculate if we have valid history
    if (oldest_temp <= 0) {
        return TEMP_TREND_STABLE;
    }
    
    float temp_rate = (current_temp - oldest_temp) / time_diff;
    
    if (temp_rate > 0.5f) {
        return TEMP_TREND_RISING;
    } else if (temp_rate < -0.5f) {
        return TEMP_TREND_FALLING;
    } else {
        return TEMP_TREND_STABLE;
    }
}