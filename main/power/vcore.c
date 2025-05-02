#include <stdio.h>
#include <math.h>
#include "esp_log.h"

#include "vcore.h"
#include "adc.h"
#include "DS4432U.h"
#include "TPS546.h"
#include "INA260.h"
#include "driver/gpio.h"

#define GPIO_ASIC_ENABLE CONFIG_GPIO_ASIC_ENABLE
#define GPIO_ASIC_RESET  CONFIG_GPIO_ASIC_RESET
#define GPIO_PLUG_SENSE  CONFIG_GPIO_PLUG_SENSE

static TPS546_CONFIG TPS546_CONFIG_GAMMATURBO = {
    /* vin voltage */
    .TPS546_INIT_VIN_ON = 11.0,
    .TPS546_INIT_VIN_OFF = 10.5,
    .TPS546_INIT_VIN_UV_WARN_LIMIT = 11.0,
    .TPS546_INIT_VIN_OV_FAULT_LIMIT = 14.0,
    /* vout voltage */
    .TPS546_INIT_SCALE_LOOP = 0.25,
    .TPS546_INIT_VOUT_MIN = 1,
    .TPS546_INIT_VOUT_MAX = 3,
    .TPS546_INIT_VOUT_COMMAND = 1.2,
    /* iout current */
    .TPS546_INIT_IOUT_OC_WARN_LIMIT = 50.00, /* A */
    .TPS546_INIT_IOUT_OC_FAULT_LIMIT = 55.00 /* A */
};

static TPS546_CONFIG TPS546_CONFIG_GAMMA = {
    /* vin voltage */
    .TPS546_INIT_VIN_ON = 4.8,
    .TPS546_INIT_VIN_OFF = 4.5,
    .TPS546_INIT_VIN_UV_WARN_LIMIT = 0, //Set to 0 to ignore. TI Bug in this register
    .TPS546_INIT_VIN_OV_FAULT_LIMIT = 6.5,
    /* vout voltage */
    .TPS546_INIT_SCALE_LOOP = 0.25,
    .TPS546_INIT_VOUT_MIN = 1,
    .TPS546_INIT_VOUT_MAX = 2,
    .TPS546_INIT_VOUT_COMMAND = 1.2,
    /* iout current */
    .TPS546_INIT_IOUT_OC_WARN_LIMIT = 25.00, /* A */
    .TPS546_INIT_IOUT_OC_FAULT_LIMIT = 30.00 /* A */
};

static const char *TAG = "vcore.c";

esp_err_t VCORE_init(GlobalState * GLOBAL_STATE) {
    switch (GLOBAL_STATE->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
            if (GLOBAL_STATE->board_version >= 402 && GLOBAL_STATE->board_version <= 499) {
                ESP_RETURN_ON_ERROR(TPS546_init(TPS546_CONFIG_GAMMA), TAG, "TPS546 init failed!"); //yes, it's a gamma as far as the TPS546 is concerned
            } else {
                ESP_RETURN_ON_ERROR(DS4432U_init(), TAG, "DS4432 init failed!");
                ESP_RETURN_ON_ERROR(INA260_init(), TAG, "INA260 init failed!");
            }
            break;
        case DEVICE_GAMMA:
            ESP_RETURN_ON_ERROR(TPS546_init(TPS546_CONFIG_GAMMA), TAG, "TPS546 init failed!");
            break;
        case DEVICE_GAMMATURBO:
            ESP_RETURN_ON_ERROR(TPS546_init(TPS546_CONFIG_GAMMATURBO), TAG, "TPS546 init failed!");
            break;
        // case DEVICE_HEX:
        default:
    }

    //configure plug sense, if present
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
                if (barrel_jack_plugged_in == 1 || GLOBAL_STATE->board_version != 204) {
                    // turn ASIC on
                    gpio_set_level(GPIO_ASIC_ENABLE, 0);
                } else {
                    // turn ASIC off
                    gpio_set_level(GPIO_ASIC_ENABLE, 1);
                }
			}
            break;
        case DEVICE_GAMMA:
        case DEVICE_GAMMATURBO:
            break;
        default:
    }

    return ESP_OK;
}

esp_err_t VCORE_set_voltage(float core_voltage, GlobalState * global_state)
{
    switch (global_state->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
            if (global_state->board_version >= 402 && global_state->board_version <= 499) {
                ESP_LOGI(TAG, "Set ASIC voltage = %.3fV", core_voltage);
                ESP_RETURN_ON_ERROR(TPS546_set_vout(core_voltage), TAG, "TPS546 set voltage failed!");
            } else {
                ESP_LOGI(TAG, "Set ASIC voltage = %.3fV", core_voltage);
                ESP_RETURN_ON_ERROR(DS4432U_set_voltage(core_voltage), TAG, "DS4432U set voltage failed!");
            }
            break;
        case DEVICE_GAMMA:
        case DEVICE_GAMMATURBO:
                ESP_LOGI(TAG, "Set ASIC voltage = %.3fV", core_voltage);
                ESP_RETURN_ON_ERROR(TPS546_set_vout(core_voltage), TAG, "TPS546 set voltage failed!");
            break;
        // case DEVICE_HEX:
        default:
    }

    return ESP_OK;
}

int16_t VCORE_get_voltage_mv(GlobalState * global_state) {

    switch (global_state->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
        case DEVICE_GAMMA:
        case DEVICE_GAMMATURBO:
            return ADC_get_vcore();
        // case DEVICE_HEX:
        default:
    }
    return -1;
}

esp_err_t VCORE_check_fault(GlobalState * global_state) {

    switch (global_state->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
            if (global_state->board_version >= 402 && global_state->board_version <= 499) {
                ESP_RETURN_ON_ERROR(TPS546_check_status(global_state), TAG, "TPS546 check status failed!");
            }
            break;
        case DEVICE_GAMMA:
        case DEVICE_GAMMATURBO:
        ESP_RETURN_ON_ERROR(TPS546_check_status(global_state), TAG, "TPS546 check status failed!");
            break;
        // case DEVICE_HEX:
        default:
    }
    return ESP_OK;
}

const char* VCORE_get_fault_string(GlobalState * global_state) {
    switch (global_state->device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
            if (global_state->board_version >= 402 && global_state->board_version <= 499) {
                return TPS546_get_error_message();
            }
            break;
        case DEVICE_GAMMA:
        case DEVICE_GAMMATURBO:
            return TPS546_get_error_message();
            break;
        // case DEVICE_HEX:
        default:
    }
    return NULL;
}

