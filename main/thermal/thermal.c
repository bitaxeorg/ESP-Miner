#include "thermal.h"

#define INTERNAL_OFFSET 5 //degrees C

esp_err_t Thermal_init(DeviceConfig device_config, bool polarity) 
{
    esp_err_t res = ESP_FAIL;

    switch(device_config.thermal) {
        case EMC2101_INTERNAL:
        case EMC2101_EXTERNAL:
            res = EMC2101_init(polarity);
            if (device_config.emc2101_config != NULL) {
                EMC2101_set_ideality_factor(device_config.emc2101_config.ideality_factory);
                EMC2101_set_beta_compensation(device_config.emc2101_config.beta_compensation);
            }
            break;
        case EMC2103:
            res = EMC2103_init(polarity);
            break;
    }

    return res;
}

//percent is a float between 0.0 and 1.0
esp_err_t Thermal_set_fan_percent(DeviceConfig device_config, float percent)
{
    switch(device_config.thermal) {
        case EMC2101_INTERNAL:
        case EMC2101_EXTERNAL:
            EMC2101_set_fan_speed(percent);
            break;
        case EMC2103:
            EMC2103_set_fan_speed(percent);
            break;
    }
    return ESP_OK;
}

uint16_t Thermal_get_fan_speed(DeviceConfig device_config) 
{
    switch(device_config.thermal) {
        case EMC2101_INTERNAL:
        case EMC2101_EXTERNAL:
            return EMC2101_get_fan_speed();
        case EMC2103:
            return EMC2103_get_fan_speed();
        default:
            return 0;
    }
}

float Thermal_get_chip_temp(GlobalState * GLOBAL_STATE) 
{
    if (!GLOBAL_STATE->ASIC_initalized) {
        return -1;
    }

    switch(GLOBAL_STATE->DEVICE_CONFIG.thermal) {
        case EMC2101_INTERNAL:
            return EMC2101_get_internal_temp() + INTERNAL_OFFSET;
        case EMC2101_EXTERNAL:
            return EMC2101_get_external_temp();
        case EMC2103:
            return EMC2103_get_external_temp();
        default:
            return -1;
    }
}
