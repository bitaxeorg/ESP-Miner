#include "thermal.h"

#include "esp_log.h"

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

thermal_temps_t Thermal_get_chip_temps(GlobalState * GLOBAL_STATE)
{
    thermal_temps_t temps = {-1.0f, -1.0f};
    
    if (!GLOBAL_STATE->ASIC_initalized) {
        return temps;
    }

    int8_t temp_offset = GLOBAL_STATE->DEVICE_CONFIG.emc_temp_offset;
    
    if (GLOBAL_STATE->DEVICE_CONFIG.EMC2103) {
        EMC2103_temps_t raw_temps = EMC2103_get_external_temps();
        temps.temp1 = raw_temps.temp1 + temp_offset;
        temps.temp2 = raw_temps.temp2 + temp_offset;
    }
    
    return temps;
}

void Thermal_get_temperatures(GlobalState * GLOBAL_STATE, float * temp1, float * temp2)
{
    if (!temp1 || !temp2) {
        return;
    }
    
    // Get primary temperature (works for both EMC2101 and EMC2103)
    *temp1 = Thermal_get_chip_temp(GLOBAL_STATE);
    
    // Only get second temperature for EMC2103 devices (GAMMA_TURBO)
    if (GLOBAL_STATE->DEVICE_CONFIG.EMC2103) {
        thermal_temps_t temps = Thermal_get_chip_temps(GLOBAL_STATE);
        *temp1 = temps.temp1;
        *temp2 = temps.temp2;
    } else {
        *temp2 = 0.0f;
    }
}

bool Thermal_has_dual_sensors(DeviceConfig * DEVICE_CONFIG)
{
    return DEVICE_CONFIG->EMC2103;
}
