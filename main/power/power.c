#include "TPS546.h"
#include "INA260.h"
#include "DS4432U.h"

#include "power.h"
#include "vcore.h"
#include "driver/gpio.h"

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
