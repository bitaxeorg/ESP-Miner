#include "thermal.h"
#include "system_module.h"
#include "esp_log.h"
#include "device_config.h"

static const char * TAG = "thermal";

esp_err_t (*set_fan_percent)(float percent);
uint16_t (*get_fan_speed)();
float (*get_chip_temp)();

esp_err_t Thermal_init()
{
    if (DEVICE_CONFIG.EMC2101) {
        ESP_LOGI(TAG, "Initializing EMC2101 (Temperature offset: %dC)", DEVICE_CONFIG.emc_temp_offset);
        esp_err_t res = EMC2101_init();
        // TODO: Improve this check.

        if (DEVICE_CONFIG.emc_ideality_factor != 0x00) {
            ESP_LOGI(TAG, "EMC2101 configuration: Ideality Factor: %02x, Beta Compensation: %02x",
                     DEVICE_CONFIG.emc_ideality_factor, DEVICE_CONFIG.emc_beta_compensation);
            EMC2101_set_ideality_factor(DEVICE_CONFIG.emc_ideality_factor);
            EMC2101_set_beta_compensation(DEVICE_CONFIG.emc_beta_compensation);
        }
        set_fan_percent = EMC2101_set_fan_speed;
        get_fan_speed = EMC2101_get_fan_speed;
        if (DEVICE_CONFIG.emc_internal_temp) {
            get_chip_temp = EMC2101_get_internal_temp;
        } else {
            get_chip_temp = EMC2101_get_external_temp;
        }
        return res;
    }
    if (DEVICE_CONFIG.EMC2103) {
        ESP_LOGI(TAG, "Initializing EMC2103 (Temperature offset: %dC)", DEVICE_CONFIG.emc_temp_offset);
        set_fan_percent = EMC2103_set_fan_speed;
        get_fan_speed = EMC2103_get_fan_speed;
        get_chip_temp = EMC2103_get_external_temp;
        return EMC2103_init();
    }

    return ESP_FAIL;
}

// percent is a float between 0.0 and 1.0
esp_err_t Thermal_set_fan_percent(float percent)
{
    return set_fan_percent(percent);
}

uint16_t Thermal_get_fan_speed()
{
    return get_fan_speed();
}

float Thermal_get_chip_temp()
{
    if (!SYSTEM_MODULE.ASIC_initalized) {
        return -1;
    }
    int8_t temp_offset = DEVICE_CONFIG.emc_temp_offset;
    return get_chip_temp() +temp_offset;
}
