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
#include <string.h>

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

void POWER_MANAGEMENT_task(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;

    power_management->frequency_multiplier = 1;

    power_management->HAS_POWER_EN = GLOBAL_STATE->board_version == 202 || GLOBAL_STATE->board_version == 203 || GLOBAL_STATE->board_version == 204;
    power_management->HAS_PLUG_SENSE = GLOBAL_STATE->board_version == 204;

    //int last_frequency_increase = 0;
    //uint16_t frequency_target = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, CONFIG_ASIC_FREQUENCY);

    uint16_t auto_fan_speed = nvs_config_get_u16(NVS_CONFIG_AUTO_FAN_SPEED, 1);

    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
			if (GLOBAL_STATE->board_version < 402 || GLOBAL_STATE->board_version > 499) {
                // Configure GPIO12 as input(barrel jack) 1 is plugged in
                gpio_config_t barrel_jack_conf = {
                    .pin_bit_mask = (1ULL << GPIO_NUM_12),
                    .mode = GPIO_MODE_INPUT,
                };
                gpio_config(&barrel_jack_conf);
                int barrel_jack_plugged_in = gpio_get_level(GPIO_NUM_12);

                gpio_set_direction(GPIO_NUM_10, GPIO_MODE_OUTPUT);
                if (barrel_jack_plugged_in == 1 || !power_management->HAS_PLUG_SENSE) {
                    // turn ASIC on
                    gpio_set_level(GPIO_NUM_10, 0);
                } else {
                    // turn ASIC off
                    gpio_set_level(GPIO_NUM_10, 1);
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
                    ESP_LOGE(TAG, "OVERHEAT ASIC %fC", power_management->chip_temp_avg );

                    EMC2101_set_fan_speed(1);
                    if (power_management->HAS_POWER_EN) {
                        gpio_set_level(GPIO_NUM_10, 1);
                    }
                    nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, 1000);
                    nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, 50);
                    nvs_config_set_u16(NVS_CONFIG_FAN_SPEED, 100);
                    nvs_config_set_u16(NVS_CONFIG_AUTO_FAN_SPEED, 0);
                    exit(EXIT_FAILURE);
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
                    ESP_LOGE(TAG, "OVERHEAT! VR: %fC ASIC %fC", power_management->vr_temp, power_management->chip_temp_avg );

                    EMC2101_set_fan_speed(1);
                    if (GLOBAL_STATE->board_version >= 402 && GLOBAL_STATE->board_version <= 499) {
                        // Turn off core voltage
                        VCORE_set_voltage(0.0, GLOBAL_STATE);
                    } else if (power_management->HAS_POWER_EN) {
                        gpio_set_level(GPIO_NUM_10, 1);
                    }
                    nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, 1000);
                    nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, 50);
                    nvs_config_set_u16(NVS_CONFIG_FAN_SPEED, 100);
                    nvs_config_set_u16(NVS_CONFIG_AUTO_FAN_SPEED, 0);
                    nvs_config_set_u16(NVS_CONFIG_OVERHEAT_MODE, 1);
                    exit(EXIT_FAILURE);
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
                    ESP_LOGE(TAG, "OVERHEAT! VR: %fC ASIC %fC", power_management->vr_temp, power_management->chip_temp_avg );

                    EMC2101_set_fan_speed(1);

                    // Turn off core voltage
                    VCORE_set_voltage(0.0, GLOBAL_STATE);

                    nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, 1000);
                    nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, 50);
                    nvs_config_set_u16(NVS_CONFIG_FAN_SPEED, 100);
                    nvs_config_set_u16(NVS_CONFIG_AUTO_FAN_SPEED, 0);
                    nvs_config_set_u16(NVS_CONFIG_OVERHEAT_MODE, 1);
                    exit(EXIT_FAILURE);
                }
                break;
            default:
        }


        if (auto_fan_speed == 1) {

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

        // Read the state of GPIO12
        if (power_management->HAS_PLUG_SENSE) {
            int gpio12_state = gpio_get_level(GPIO_NUM_12);
            if (gpio12_state == 0) {
                // turn ASIC off
                gpio_set_level(GPIO_NUM_10, 1);
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

        vTaskDelay(POLL_RATE / portTICK_PERIOD_MS);
    }
}
