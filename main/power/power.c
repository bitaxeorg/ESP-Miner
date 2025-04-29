#include "TPS546.h"
#include "INA260.h"
#include "DS4432U.h"

#include "power.h"
#include "vcore.h"
#include "esp_log.h"

#define GPIO_ASIC_ENABLE CONFIG_GPIO_ASIC_ENABLE

#define SUPRA_POWER_OFFSET 5 //Watts
#define GAMMA_POWER_OFFSET 5 //Watts
#define GAMMATURBO_POWER_OFFSET 5 //Watts

// max power settings
#define MAX_MAX_POWER 25 //watts
#define ULTRA_MAX_POWER 25 //Watts
#define SUPRA_MAX_POWER 40 //watts
#define GAMMA_MAX_POWER 40 //Watts
#define GAMMATURBO_MAX_POWER 60 //Watts

// nominal voltage settings
#define NOMINAL_VOLTAGE_5 5 //volts
#define NOMINAL_VOLTAGE_12 12//volts

static const char *TAG = "power";

// Voltage-frequency curve coefficients
// These coefficients define the relationship between frequency and minimum required voltage
#define VF_CURVE_SLOPE_BM1366 0.0008f  // Voltage increase per MHz
#define VF_CURVE_SLOPE_BM1368 0.0008f
#define VF_CURVE_SLOPE_BM1370 0.0007f
#define VF_CURVE_SLOPE_BM1397 0.0009f

#define VF_CURVE_BASE_BM1366 0.8f  // Base voltage at 0 MHz
#define VF_CURVE_BASE_BM1368 0.8f
#define VF_CURVE_BASE_BM1370 0.8f
#define VF_CURVE_BASE_BM1397 0.8f

// Minimum voltage regardless of frequency
#define MIN_VOLTAGE_BM1366 0.92f
#define MIN_VOLTAGE_BM1368 0.92f
#define MIN_VOLTAGE_BM1370 0.92f
#define MIN_VOLTAGE_BM1397 0.92f

// Safety margin for voltage (multiplier)
#define VOLTAGE_SAFETY_MARGIN 1.05f

esp_err_t Power_disable(GlobalState * GLOBAL_STATE) {

    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
            if (GLOBAL_STATE->board_version >= 402 && GLOBAL_STATE->board_version <= 499) {
                // Turn off core voltage
                VCORE_set_voltage(0.0, GLOBAL_STATE);
            } else if (GLOBAL_STATE->board_version == 202 || GLOBAL_STATE->board_version == 203 || GLOBAL_STATE->board_version == 204) {
                gpio_set_level(GPIO_ASIC_ENABLE, 1);
            }
            break;
        case DEVICE_GAMMA:
        case DEVICE_GAMMATURBO:
            // Turn off core voltage
            VCORE_set_voltage(0.0, GLOBAL_STATE);
            break;
        default:
    }
    return ESP_OK;

}

float Power_get_max_settings(GlobalState * GLOBAL_STATE) {

    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
            return MAX_MAX_POWER;
        case DEVICE_ULTRA:
            return ULTRA_MAX_POWER;
        case DEVICE_SUPRA:
            return SUPRA_MAX_POWER;
        case DEVICE_GAMMA:
            return GAMMA_MAX_POWER;
        case DEVICE_GAMMATURBO:
            return GAMMATURBO_MAX_POWER;
        default:
        return GAMMA_MAX_POWER;
    }
}

float Power_get_current(GlobalState * GLOBAL_STATE) {
    float current = 0.0;

    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
            if (GLOBAL_STATE->board_version >= 402 && GLOBAL_STATE->board_version <= 499) {
                current = TPS546_get_iout() * 1000.0;
            } else {
                if (INA260_installed() == true) {
                    current = INA260_read_current();
                }
            }
            break;
        case DEVICE_GAMMA:
        case DEVICE_GAMMATURBO:
            current = TPS546_get_iout() * 1000.0;
            break;
        default:
    }

    return current;
}

float Power_get_power(GlobalState * GLOBAL_STATE) {
    float power = 0.0;
    float current = 0.0;

    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
            if (GLOBAL_STATE->board_version >= 402 && GLOBAL_STATE->board_version <= 499) {
                current = TPS546_get_iout() * 1000.0;
                // calculate regulator power (in milliwatts)
                power = (TPS546_get_vout() * current) / 1000.0;
                // The power reading from the TPS546 is only it's output power. So the rest of the Bitaxe power is not accounted for.
                power += SUPRA_POWER_OFFSET; // Add offset for the rest of the Bitaxe power. TODO: this better.
            } else {
                if (INA260_installed() == true) {
                    power = INA260_read_power() / 1000.0;
                }
            }
        
            break;
        case DEVICE_GAMMA:
        case DEVICE_GAMMATURBO:
                current = TPS546_get_iout() * 1000.0;
                // calculate regulator power (in milliwatts)
                power = (TPS546_get_vout() * current) / 1000.0;
                // The power reading from the TPS546 is only it's output power. So the rest of the Bitaxe power is not accounted for.
                power += GAMMATURBO_POWER_OFFSET; // Add offset for the rest of the Bitaxe power. TODO: this better.
            break;
        default:
    }

    return power;
}


float Power_get_input_voltage(GlobalState * GLOBAL_STATE) {

    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
            if (GLOBAL_STATE->board_version >= 402 && GLOBAL_STATE->board_version <= 499) {
                return TPS546_get_vin() * 1000.0;
            } else {
                if (INA260_installed() == true) {
                    return INA260_read_voltage();
                }
            }
        
            break;
        case DEVICE_GAMMA:
        case DEVICE_GAMMATURBO:
                return TPS546_get_vin() * 1000.0;
            break;
        default:
    }

    return 0.0;
}

int Power_get_nominal_voltage(GlobalState * GLOBAL_STATE) {
    switch (GLOBAL_STATE->device_model)
    {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
        case DEVICE_GAMMA:
            return NOMINAL_VOLTAGE_5;
        case DEVICE_GAMMATURBO:
            return NOMINAL_VOLTAGE_12;
        default:
        return NOMINAL_VOLTAGE_5;
    }
}

float Power_get_vreg_temp(GlobalState * GLOBAL_STATE) {

    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
            if (GLOBAL_STATE->board_version >= 402 && GLOBAL_STATE->board_version <= 499) {
                return TPS546_get_temperature();
            } else {
                if (INA260_installed() == true) {
                    return 0.0;
                }
            }
        
            break;
        case DEVICE_GAMMA:
        case DEVICE_GAMMATURBO:
                return TPS546_get_temperature();
            break;
        default:
    }

    return 0.0;
}

// Calculate minimum voltage required for a given frequency based on ASIC model
float Power_calculate_min_voltage(GlobalState * GLOBAL_STATE, float frequency) {
    float slope = 0.0f;
    float base = 0.0f;
    float min_voltage = 0.0f;
    
    // Select appropriate voltage-frequency curve based on ASIC model
    switch (GLOBAL_STATE->asic_model) {
        case ASIC_BM1366:
            slope = VF_CURVE_SLOPE_BM1366;
            base = VF_CURVE_BASE_BM1366;
            min_voltage = MIN_VOLTAGE_BM1366;
            break;
        case ASIC_BM1368:
            slope = VF_CURVE_SLOPE_BM1368;
            base = VF_CURVE_BASE_BM1368;
            min_voltage = MIN_VOLTAGE_BM1368;
            break;
        case ASIC_BM1370:
            slope = VF_CURVE_SLOPE_BM1370;
            base = VF_CURVE_BASE_BM1370;
            min_voltage = MIN_VOLTAGE_BM1370;
            break;
        case ASIC_BM1397:
            slope = VF_CURVE_SLOPE_BM1397;
            base = VF_CURVE_BASE_BM1397;
            min_voltage = MIN_VOLTAGE_BM1397;
            break;
        default:
            // Default to most conservative values
            slope = 0.001f;
            base = 0.9f;
            min_voltage = 1.0f;
    }
    
    // Calculate voltage using linear V/F model: V = base + (slope * frequency)
    float calculated_voltage = base + (slope * frequency);
    
    // Apply safety margin
    calculated_voltage *= VOLTAGE_SAFETY_MARGIN;
    
    // Don't go below minimum voltage
    if (calculated_voltage < min_voltage) {
        calculated_voltage = min_voltage;
    }
    
    ESP_LOGI(TAG, "Calculated voltage for %.2f MHz: %.3f V", frequency, calculated_voltage);
    
    return calculated_voltage;
}

// Set optimal voltage for current frequency
esp_err_t Power_optimize_voltage(GlobalState * GLOBAL_STATE) {
    if (!GLOBAL_STATE->POWER_MANAGEMENT_MODULE.dynamic_voltage) {
        return ESP_OK; // Skip if dynamic voltage optimization is disabled
    }
    
    float current_frequency = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.current_frequency;
    if (current_frequency <= 0) {
        current_frequency = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value;
    }
    
    // Calculate the optimal voltage for this frequency
    float optimal_voltage = Power_calculate_min_voltage(GLOBAL_STATE, current_frequency);
    
    // Apply user-defined offset (can be negative for undervolting)
    optimal_voltage += GLOBAL_STATE->POWER_MANAGEMENT_MODULE.voltage_offset;
    
    // Ensure we don't go below absolute minimum
    if (optimal_voltage < 0.85f) {
        optimal_voltage = 0.85f;
    }
    
    // Set the new voltage if it differs from current setting
    float current_voltage = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.voltage / 1000.0f;
    if (fabs(optimal_voltage - current_voltage) > 0.005f) {
        ESP_LOGI(TAG, "Adjusting voltage from %.3f V to %.3f V", current_voltage, optimal_voltage);
        GLOBAL_STATE->POWER_MANAGEMENT_MODULE.target_voltage = optimal_voltage;
        return VCORE_set_voltage(optimal_voltage, GLOBAL_STATE);
    }
    
    return ESP_OK;
}

// Calculate power efficiency (hashrate per watt)
float Power_calculate_efficiency(GlobalState * GLOBAL_STATE) {
    float power = Power_get_power(GLOBAL_STATE);
    float hashrate = GLOBAL_STATE->SYSTEM_MODULE.current_hashrate;
    
    if (power > 0.5f) {  // Avoid division by zero or meaningless values
        return hashrate / power;  // TH/s per watt
    }
    
    return 0.0f;
}

// Apply power profile settings
esp_err_t Power_apply_profile(GlobalState * GLOBAL_STATE, PowerProfile profile) {
    GLOBAL_STATE->SYSTEM_MODULE.power_profile = profile;
    
    switch (profile) {
        case POWER_PROFILE_PERFORMANCE:
            // Maximum performance settings
            GLOBAL_STATE->POWER_MANAGEMENT_MODULE.dynamic_voltage = false;
            GLOBAL_STATE->POWER_MANAGEMENT_MODULE.voltage_offset = 0.03f;  // +30mV for stability
            break;
            
        case POWER_PROFILE_BALANCED:
            // Balanced settings
            GLOBAL_STATE->POWER_MANAGEMENT_MODULE.dynamic_voltage = true;
            GLOBAL_STATE->POWER_MANAGEMENT_MODULE.voltage_offset = 0.01f;  // +10mV
            break;
            
        case POWER_PROFILE_EFFICIENCY:
            // Maximum efficiency settings
            GLOBAL_STATE->POWER_MANAGEMENT_MODULE.dynamic_voltage = true;
            GLOBAL_STATE->POWER_MANAGEMENT_MODULE.voltage_offset = -0.01f; // -10mV (slight undervolt)
            break;
            
        case POWER_PROFILE_DYNAMIC:
            // Will adjust based on conditions
            GLOBAL_STATE->POWER_MANAGEMENT_MODULE.dynamic_voltage = true;
            GLOBAL_STATE->POWER_MANAGEMENT_MODULE.voltage_offset = 0.0f;
            break;
            
        default:
            return ESP_ERR_INVALID_ARG;
    }
    
    // Apply the new settings
    return Power_optimize_voltage(GLOBAL_STATE);
}
