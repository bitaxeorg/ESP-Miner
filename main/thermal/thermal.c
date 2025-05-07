#include "thermal.h"

#define INTERNAL_OFFSET 5 //degrees C


esp_err_t Thermal_init(DeviceModel device_model) {
    esp_err_t err = ESP_OK;
    
    //init the EMC2101, if we have one
    switch (device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
            err = EMC2101_init();
            if (err != ESP_OK) {
                return err;
            }
            break;
        case DEVICE_GAMMA:
            err = EMC2101_init();
            if (err != ESP_OK) {
                return err;
            }
            
            err = EMC2101_set_ideality_factor(EMC2101_IDEALITY_1_0319);
            if (err != ESP_OK) {
                return err;
            }
            
            err = EMC2101_set_beta_compensation(EMC2101_BETA_11);
            if (err != ESP_OK) {
                return err;
            }
            break;
        case DEVICE_GAMMATURBO:
            err = EMC2103_init();
            if (err != ESP_OK) {
                return err;
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

//percent is a float between 0.0 and 1.0
esp_err_t Thermal_set_fan_percent(DeviceModel device_model, float percent) {
    esp_err_t err = ESP_OK;

    switch (device_model) {
        case DEVICE_MAX:
        case DEVICE_ULTRA:
        case DEVICE_SUPRA:
        case DEVICE_GAMMA:
            err = EMC2101_set_fan_speed(percent);
            if (err != ESP_OK) {
                return err;
            }
            break;
        case DEVICE_GAMMATURBO:
            err = EMC2103_set_fan_speed(percent);
            if (err != ESP_OK) {
                return err;
            }
            break;
        default:
            break;
    }
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