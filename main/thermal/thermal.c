#include "thermal.h"

#include "esp_log.h"
#include "EMC2101.h"
#include "EMC2103.h"

static const char * TAG = "thermal";

esp_err_t Thermal_init(DeviceConfig * DEVICE_CONFIG)
{
    if (DEVICE_CONFIG->EMC2101) {
        ESP_LOGI(TAG, "Initializing EMC2101 (Temperature offset: %dC)", DEVICE_CONFIG->emc_temp_offset);
        esp_err_t res = EMC2101_init();
        // TODO: Improve this check.
        if (DEVICE_CONFIG->emc_ideality_factor != 0x00) {
            ESP_LOGI(TAG, "EMC2101 configuration: Ideality Factor: %02x, Beta Compensation: %02x", DEVICE_CONFIG->emc_ideality_factor, DEVICE_CONFIG->emc_beta_compensation);
            EMC2101_set_ideality_factor(DEVICE_CONFIG->emc_ideality_factor);
            EMC2101_set_beta_compensation(DEVICE_CONFIG->emc_beta_compensation);
        }
        return res;
    }
    if (DEVICE_CONFIG->EMC2103) {
        ESP_LOGI(TAG, "Initializing EMC2103 (Temperature offset: %dC)", DEVICE_CONFIG->emc_temp_offset);
        return EMC2103_init();
    }

    return ESP_FAIL;
}

//percent is a float between 0.0 and 1.0
esp_err_t Thermal_set_fan_percent(DeviceConfig * DEVICE_CONFIG, float percent)
{
    if (DEVICE_CONFIG->EMC2101) {
        EMC2101_set_fan_speed(percent);
    }
    if (DEVICE_CONFIG->EMC2103) {
        EMC2103_set_fan_speed(percent);
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

    int8_t temp_offset = GLOBAL_STATE->DEVICE_CONFIG.emc_temp_offset;
    if (GLOBAL_STATE->DEVICE_CONFIG.EMC2101) {
        if (GLOBAL_STATE->DEVICE_CONFIG.emc_internal_temp) {
            return EMC2101_get_internal_temp() + temp_offset;
        } else {
            return EMC2101_get_external_temp() + temp_offset;
        }
    }
    if (GLOBAL_STATE->DEVICE_CONFIG.EMC2103) {
        return EMC2103_get_external_temp() + temp_offset;
    }
    return -1;
}

float Thermal_get_chip_temp2(GlobalState * GLOBAL_STATE)
{
    if (!GLOBAL_STATE->ASIC_initalized) {
        return -1;
    }

    int8_t temp_offset = GLOBAL_STATE->DEVICE_CONFIG.emc_temp_offset;
    if (GLOBAL_STATE->DEVICE_CONFIG.EMC2103) {
        return EMC2103_get_external_temp2() + temp_offset;
    }
    return -1;
}
