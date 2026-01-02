#include <string.h>
#include <stdlib.h>
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
#include "power_management/power_management_calc.h"
#include "power_management/autotune_state.h"
#include "power_management/overheat.h"
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

/* Thread-safe autotune state - initialized in POWER_MANAGEMENT_task */
static autotune_state_t s_autotune_state = NULL;

/**
 * @brief Check for overheat and execute recovery if needed
 *
 * Uses the consolidated overheat module to check temperatures and
 * trigger appropriate recovery actions.
 *
 * @param GLOBAL_STATE Global state pointer
 * @param chip_temp Current chip temperature
 * @param vr_temp Current VR temperature (0 if not available)
 * @param frequency Current frequency
 * @param voltage Current voltage
 * @param device_name Device name for logging
 */
static void check_and_handle_overheat(GlobalState* GLOBAL_STATE,
                                       float chip_temp, float vr_temp,
                                       uint16_t frequency, uint16_t voltage,
                                       const char* device_name)
{
    PowerManagementModule *power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;

    /* Build input for overheat check */
    overheat_check_input_t input = {
        .chip_temp = chip_temp,
        .vr_temp = vr_temp,
        .frequency = frequency,
        .voltage = voltage
    };

    /* Get current overheat count */
    uint16_t overheat_count = nvs_config_get_u16(NVS_CONFIG_OVERHEAT_COUNT, 0);

    /* Check for overheat condition */
    overheat_check_result_t result = overheat_check(&input, overheat_count);

    if (!result.should_trigger) {
        return;
    }

    /* Build device config for recovery */
    overheat_device_config_t config = {
        .device_model = GLOBAL_STATE->device_model,
        .board_version = GLOBAL_STATE->board_version,
        .has_power_en = power_management->HAS_POWER_EN,
        .has_tps546 = (GLOBAL_STATE->board_version >= 402 && GLOBAL_STATE->board_version <= 499)
    };

    /* Format log data */
    char log_data[128];
    overheat_format_log_data(log_data, sizeof(log_data), &input, device_name);

    /* Log the event */
    char device_info[64];
    overheat_format_device_info(device_info, sizeof(device_info), &input, device_name);

    if (result.severity == PM_SEVERITY_HARD) {
        ESP_LOGE(TAG, "Overheat event #%u (multiple of 3), using hard recovery", overheat_count + 1);
        ESP_LOGE(TAG, "HARD OVERHEAT RECOVERY: %s", device_info);
    } else {
        ESP_LOGE(TAG, "OVERHEAT DETECTED: %s", device_info);
    }

    /* Execute recovery */
    overheat_execute_recovery(
        result.severity,
        &config,
        NULL,  /* Use default safe values */
        overheat_get_default_hw_ops(),
        GLOBAL_STATE,  /* Context for VCORE operations */
        result.overheat_type,
        log_data
    );

    /* Note: For hard recovery, we won't return from overheat_execute_recovery
     * as the task will be deleted. For soft recovery, the system will restart. */
}

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

/**
 * @brief Autotune function using pure calculation functions and thread-safe state
 *
 * This refactored version fixes race conditions by:
 * 1. Capturing all readings atomically at the start
 * 2. Using thread-safe state for timing and counters
 * 3. Delegating calculations to pure functions for testability
 */
static void autotuneOffset(GlobalState * GLOBAL_STATE)
{
    static const char *autotuneTAG = "autotune";

    /* Validate autotune state is initialized */
    if (!autotune_state_is_valid(s_autotune_state)) {
        ESP_LOGE(autotuneTAG, "Autotune state not initialized");
        return;
    }

    /* Check if autotune is enabled */
    if (nvs_config_get_u16(NVS_CONFIG_AUTOTUNE_FLAG, 1) == 0) {
        ESP_LOGI(autotuneTAG, "Autotune is disabled");
        return;
    }

    /* Access modules for reading (minimize time holding implicit locks) */
    AutotuneModule *autotune = &GLOBAL_STATE->AUTOTUNE_MODULE;
    PowerManagementModule *power = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    SystemModule *system = &GLOBAL_STATE->SYSTEM_MODULE;

    /*
     * CRITICAL: Capture all readings atomically at the start.
     * This prevents race conditions where values change mid-calculation.
     */
    uint16_t currentDomainVoltage = VCORE_get_voltage_mv(GLOBAL_STATE);
    uint16_t currentFrequency = (uint16_t)power->frequency_value;
    float chipTempAvg = power->chip_temp_avg;
    float currentHashrate = system->current_hashrate;
    int16_t currentPower = (int16_t)power->power;

    /* Calculate target hashrate using pure function */
    float targetHashrate = pm_calc_target_hashrate(currentFrequency,
                                                    GLOBAL_STATE->small_core_count,
                                                    GLOBAL_STATE->asic_count);

    /* Build input struct for pure calculation function */
    pm_autotune_input_t input = {
        .chip_temp = chipTempAvg,
        .current_hashrate = currentHashrate,
        .target_hashrate = targetHashrate,
        .current_frequency = currentFrequency,
        .current_voltage = currentDomainVoltage,
        .current_power = currentPower,
        .uptime_seconds = (esp_timer_get_time() - system->start_time) / 1000000
    };

    /* Build limits struct from autotune module */
    pm_autotune_limits_t limits = {
        .max_frequency = autotune->maxFrequency,
        .min_frequency = autotune->minFrequency,
        .max_voltage = autotune->maxDomainVoltage,
        .min_voltage = autotune->minDomainVoltage,
        .max_power = autotune->maxPower
    };

    /* Log current values */
    ESP_LOGI(autotuneTAG, "Autotune - Current Values:");
    ESP_LOGI(autotuneTAG, "  Domain Voltage: %u mV", currentDomainVoltage);
    ESP_LOGI(autotuneTAG, "  Frequency: %u MHz", currentFrequency);
    ESP_LOGI(autotuneTAG, "  ASIC Temp: %.1f °C", chipTempAvg);
    ESP_LOGI(autotuneTAG, "  Hashrate: %.2f GH/s", currentHashrate);
    ESP_LOGI(autotuneTAG, "  Power: %d W", currentPower);
    ESP_LOGI(autotuneTAG, "  Max Power: %d W", limits.max_power);
    ESP_LOGI(autotuneTAG, "  Limits: Freq[%u-%u], Volt[%u-%u]",
             limits.min_frequency, limits.max_frequency,
             limits.min_voltage, limits.max_voltage);

    ESP_LOGI(autotuneTAG, "Autotune - Target Values:");
    ESP_LOGI(autotuneTAG, "  Target Temperature: %u °C", PM_AUTOTUNE_TARGET_TEMP);
    ESP_LOGI(autotuneTAG, "  Target Hashrate: %.2f GH/s", targetHashrate);

    /* Get timing info from thread-safe state */
    uint32_t currentTickMs = xTaskGetTickCount() * portTICK_PERIOD_MS;
    uint32_t msSinceLastAdjust = autotune_state_get_ms_since_last_adjust(s_autotune_state, currentTickMs);
    uint8_t consecutiveLowHashrate = autotune_state_get_low_hashrate_count(s_autotune_state);

    /* Call pure calculation function */
    pm_autotune_decision_t decision = pm_calc_autotune(&input, &limits,
                                                        PM_AUTOTUNE_TARGET_TEMP,
                                                        consecutiveLowHashrate,
                                                        msSinceLastAdjust);

    /* Handle skip reasons */
    if (decision.skip_reason_invalid) {
        if ((uint8_t)chipTempAvg == 255) {
            ESP_LOGI(autotuneTAG, "Skipping autotune - Temperature sensor not initialized");
        } else if (currentHashrate <= 0) {
            ESP_LOGI(autotuneTAG, "Skipping autotune - Hashrate is 0");
        }
        return;
    }

    if (decision.skip_reason_warmup) {
        uint32_t remaining = PM_AUTOTUNE_WARMUP_SECONDS - input.uptime_seconds;
        ESP_LOGI(autotuneTAG, "Autotune - Waiting for initial warmup period (%lu seconds remaining)", remaining);
        return;
    }

    if (decision.skip_reason_timing) {
        uint32_t interval = pm_get_autotune_interval_ms(chipTempAvg);
        uint32_t remaining = interval - msSinceLastAdjust;
        ESP_LOGI(autotuneTAG, "Autotune - Waiting for next adjustment interval (%lu ms remaining)", remaining);
        return;
    }

    /* Update timing - we're going to process this cycle */
    autotune_state_update_last_adjust_time(s_autotune_state, currentTickMs);

    /* Check for low hashrate condition */
    if (pm_is_hashrate_low(currentHashrate, targetHashrate, PM_HASHRATE_THRESHOLD_PERCENT)) {
        uint8_t newCount = autotune_state_increment_low_hashrate(s_autotune_state);
        ESP_LOGI(autotuneTAG, "Low hashrate detected: %.2f GH/s (threshold: %.2f%% of %.2f), consecutive: %u",
                 currentHashrate, PM_HASHRATE_THRESHOLD_PERCENT, targetHashrate, newCount);

        char data[128];
        snprintf(data, sizeof(data), "{\"currentHashrate\":%.2f,\"targetHashrate\":%.2f,\"consecutiveAttempts\":%u}",
                 currentHashrate, targetHashrate, newCount);
        dataBase_log_event("power", "warn", "Autotune - Low hashrate detected", data);
    } else {
        /* Hashrate is good, reset counter if needed */
        if (consecutiveLowHashrate > 0) {
            ESP_LOGI(autotuneTAG, "Hashrate recovered: %.2f GH/s, resetting counter", currentHashrate);
            autotune_state_reset_low_hashrate(s_autotune_state);
        }
    }

    /* Handle preset reset (3 consecutive low hashrate events) */
    if (decision.should_reset_preset) {
        char current_preset[32];
        char* preset_ptr = nvs_config_get_string(NVS_CONFIG_AUTOTUNE_PRESET, "balanced");
        strncpy(current_preset, preset_ptr, sizeof(current_preset) - 1);
        current_preset[sizeof(current_preset) - 1] = '\0';
        free(preset_ptr);

        ESP_LOGE(autotuneTAG, "SAFETY: %u consecutive low hashrate attempts, reapplying preset '%s'",
                 PM_MAX_LOW_HASHRATE_ATTEMPTS, current_preset);

        char safety_data[256];
        snprintf(safety_data, sizeof(safety_data),
                 "{\"consecutiveAttempts\":%u,\"currentHashrate\":%.2f,\"targetHashrate\":%.2f,\"preset\":\"%s\"}",
                 PM_MAX_LOW_HASHRATE_ATTEMPTS, currentHashrate, targetHashrate, current_preset);
        dataBase_log_event("power", "critical", "Autotune safety reset - consecutive low hashrate attempts", safety_data);

        if (apply_preset(GLOBAL_STATE->device_model, current_preset)) {
            ESP_LOGI(autotuneTAG, "Successfully reapplied preset '%s'", current_preset);
        } else {
            ESP_LOGE(autotuneTAG, "Failed to reapply preset '%s'", current_preset);
        }

        autotune_state_reset_low_hashrate(s_autotune_state);
        return;
    }

    /* Apply adjustments if needed */
    if (!decision.should_adjust) {
        ESP_LOGI(autotuneTAG, "Autotune - No adjustments needed");
        return;
    }

    /* Apply frequency change */
    if (decision.new_frequency != 0 && decision.new_frequency != currentFrequency) {
        ESP_LOGI(autotuneTAG, "Autotune - Adjusting frequency from %u MHz to %u MHz",
                 currentFrequency, decision.new_frequency);
        nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, decision.new_frequency);
    }

    /* Apply voltage change */
    if (decision.new_voltage != 0 && decision.new_voltage != currentDomainVoltage) {
        ESP_LOGI(autotuneTAG, "Autotune - Adjusting voltage from %u mV to %u mV",
                 currentDomainVoltage, decision.new_voltage);
        nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, decision.new_voltage);
    }

    /* Log the adjustment */
    char data[192];
    snprintf(data, sizeof(data),
             "{\"newFrequency\":%u,\"newVoltage\":%u,\"temperature\":%.1f,\"hashrate\":%.2f,\"targetHashrate\":%.2f}",
             decision.new_frequency ? decision.new_frequency : currentFrequency,
             decision.new_voltage ? decision.new_voltage : currentDomainVoltage,
             chipTempAvg, currentHashrate, targetHashrate);
    dataBase_log_event("power", "info", "Autotune - Applied adjustments", data);
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

/* NOTE: handle_hard_overheat_recovery and handle_overheat_recovery functions
 * have been removed and replaced by the consolidated overheat module.
 * See: main/power_management/overheat.h and overheat.c
 */

void POWER_MANAGEMENT_task(void * pvParameters)
{
    ESP_LOGI(TAG, "Starting");

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    /* Initialize thread-safe autotune state */
    if (s_autotune_state == NULL) {
        s_autotune_state = autotune_state_create();
        if (s_autotune_state == NULL) {
            ESP_LOGE(TAG, "Failed to create autotune state - autotune will be disabled");
        } else {
            ESP_LOGI(TAG, "Autotune state initialized");
        }
    }

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

        /* Temperature reading and overheat handling - consolidated using overheat module */
        switch (GLOBAL_STATE->device_model) {
            case DEVICE_MAX:
                power_management->chip_temp_avg = GLOBAL_STATE->ASIC_initalized ? EMC2101_get_external_temp() : -1;
                power_management->vr_temp = 0.0f;  /* MAX has no VR temp sensor */

                check_and_handle_overheat(GLOBAL_STATE,
                    power_management->chip_temp_avg, 0.0f,
                    (uint16_t)power_management->frequency_value,
                    power_management->voltage, "DEVICE_MAX");
                break;

            case DEVICE_ULTRA:
            case DEVICE_SUPRA:
                if (GLOBAL_STATE->board_version >= 402 && GLOBAL_STATE->board_version <= 499) {
                    power_management->chip_temp_avg = GLOBAL_STATE->ASIC_initalized ? EMC2101_get_external_temp() : -1;
                    power_management->vr_temp = (float)TPS546_get_temperature();
                } else {
                    power_management->chip_temp_avg = EMC2101_get_internal_temp() + 5;
                    power_management->vr_temp = 0.0f;
                }

                /* EMC2101 will give bad readings if the ASIC is turned off */
                if (power_management->voltage < TPS546_INIT_VOUT_MIN) {
                    break;
                }

                check_and_handle_overheat(GLOBAL_STATE,
                    power_management->chip_temp_avg, power_management->vr_temp,
                    (uint16_t)power_management->frequency_value,
                    power_management->voltage, "DEVICE_ULTRA/SUPRA");
                break;

            case DEVICE_GAMMA:
                power_management->chip_temp_avg = GLOBAL_STATE->ASIC_initalized ? EMC2101_get_external_temp() : -1;
                power_management->vr_temp = (float)TPS546_get_temperature();

                /* EMC2101 will give bad readings if the ASIC is turned off */
                if (power_management->voltage < TPS546_INIT_VOUT_MIN) {
                    break;
                }

                check_and_handle_overheat(GLOBAL_STATE,
                    power_management->chip_temp_avg, power_management->vr_temp,
                    (uint16_t)power_management->frequency_value,
                    power_management->voltage, "DEVICE_GAMMA");
                break;

            default:
                break;
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
