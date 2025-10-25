#include "thermal.h"

#include "esp_log.h"
#include "EMC2101.h"
#include "EMC2103.h"

static const char * TAG = "thermal";

esp_err_t Thermal_init(DeviceConfig * DEVICE_CONFIG)
{
    if (DEVICE_CONFIG->EMC2101) {
        esp_err_t res = EMC2101_init(DEVICE_CONFIG->emc_temp_offset);
        // TODO: Improve this check.
        if (DEVICE_CONFIG->emc_ideality_factor != 0x00) {
            ESP_LOGI(TAG, "EMC2101 configuration: Ideality Factor: %02x, Beta Compensation: %02x", DEVICE_CONFIG->emc_ideality_factor, DEVICE_CONFIG->emc_beta_compensation);
            EMC2101_set_ideality_factor(DEVICE_CONFIG->emc_ideality_factor);
            EMC2101_set_beta_compensation(DEVICE_CONFIG->emc_beta_compensation);
        }
        return res;
    }
    if (DEVICE_CONFIG->EMC2103) {
        return EMC2103_init(DEVICE_CONFIG->emc_temp_offset);
    }

    return ESP_FAIL;
}

//percent is a float between 0.0 and 1.0
esp_err_t Thermal_set_fan_percent(DeviceConfig * DEVICE_CONFIG, float percent)
{
    if (DEVICE_CONFIG->EMC2101) {
        return EMC2101_set_fan_speed(percent);
    }
    if (DEVICE_CONFIG->EMC2103) {
        return EMC2103_set_fan_speed(percent);
    }
    return ESP_OK;
}

uint16_t Thermal_get_fan_speed(DeviceConfig * DEVICE_CONFIG) 
{
    if (DEVICE_CONFIG->EMC2101) {
        return EMC2101_get_fan_speed();
    }
    if (DEVICE_CONFIG->EMC2103) {
        return EMC2103_get_fan_speed();
    }
    return 0;
}

float Thermal_get_chip_temp(GlobalState * GLOBAL_STATE)
{
    if (!GLOBAL_STATE->ASIC_initalized) {
        return -1;
    }

    if (GLOBAL_STATE->DEVICE_CONFIG.EMC2101) {
        if (GLOBAL_STATE->DEVICE_CONFIG.emc_internal_temp) {
            return EMC2101_get_internal_temp();
        } else {
            return EMC2101_get_external_temp();
        }
    }
    if (GLOBAL_STATE->DEVICE_CONFIG.EMC2103) {
        return EMC2103_get_external_temp();
    }
    return -1;
}

float Thermal_get_chip_temp2(GlobalState * GLOBAL_STATE)
{
    if (!GLOBAL_STATE->ASIC_initalized) {
        return -1;
    }

    if (GLOBAL_STATE->DEVICE_CONFIG.EMC2103) {
        return EMC2103_get_external_temp2();
    }
    return -1;
}
