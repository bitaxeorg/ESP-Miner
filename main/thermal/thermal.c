#include "thermal.h"

#include "esp_log.h"

static const char * TAG = "thermal";

esp_err_t Thermal_init(BoardConfig * BOARD_CONFIG)
{
    if (BOARD_CONFIG->EMC2101) {
        ESP_LOGI(TAG, "Initializing EMC2101 (Temperature offset: %dC)", BOARD_CONFIG->emc_temp_offset);
        esp_err_t res = EMC2101_init();
        // TODO: Improve this check.
        if (BOARD_CONFIG->emc_ideality_factor != 0x00) {
            ESP_LOGI(TAG, "EMC2101 configuration: Ideality Factor: %02x, Beta Compensation: %02x", BOARD_CONFIG->emc_ideality_factor, BOARD_CONFIG->emc_beta_compensation);
            EMC2101_set_ideality_factor(BOARD_CONFIG->emc_ideality_factor);
            EMC2101_set_beta_compensation(BOARD_CONFIG->emc_beta_compensation);
        }
        return res;
    }
    if (BOARD_CONFIG->EMC2103) {
        ESP_LOGI(TAG, "Initializing EMC2103 (Temperature offset: %dC)", BOARD_CONFIG->emc_temp_offset);
        return EMC2103_init();
    }

    return ESP_FAIL;
}

//percent is a float between 0.0 and 1.0
esp_err_t Thermal_set_fan_percent(BoardConfig * BOARD_CONFIG, float percent)
{
    if (BOARD_CONFIG->EMC2101) {
        EMC2101_set_fan_speed(percent);
    }
    if (BOARD_CONFIG->EMC2103) {
        EMC2103_set_fan_speed(percent);
    }
    return ESP_OK;
}

uint16_t Thermal_get_fan_speed(BoardConfig * BOARD_CONFIG) 
{
    if (BOARD_CONFIG->EMC2101) {
        return EMC2101_get_fan_speed();
    }
    if (BOARD_CONFIG->EMC2103) {
        return EMC2103_get_fan_speed();
    }
    return 0;
}

float Thermal_get_chip_temp(BoardConfig * BOARD_CONFIG)
{
    int8_t temp_offset = BOARD_CONFIG->emc_temp_offset;
    if (BOARD_CONFIG->EMC2101) {
        if (BOARD_CONFIG->emc_internal_temp) {
            return EMC2101_get_internal_temp() + temp_offset;
        } else {
            return EMC2101_get_external_temp() + temp_offset;
        }
    }
    if (BOARD_CONFIG->EMC2103) {
        return EMC2103_get_external_temp() + temp_offset;
    }
    return -1;
}
