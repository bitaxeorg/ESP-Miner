#include <string.h>
#include "device_config.h"
#include "nvs_config.h"
#include "global_state.h"
#include "esp_log.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static const char * TAG = "device_config";

esp_err_t device_config_init(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    // TODO: Read board version from eFuse

    char * board_version = nvs_config_get_string(NVS_CONFIG_BOARD_VERSION, "000");

    for (int i = 0 ; i < ARRAY_SIZE(default_configs); i++) {
        if (strcmp(default_configs[i].board_version, board_version) == 0) {
            GLOBAL_STATE->DEVICE_CONFIG = default_configs[i];

            ESP_LOGI(TAG, "Device Model: %s", GLOBAL_STATE->DEVICE_CONFIG.family.name);
            ESP_LOGI(TAG, "Board Version: %s", GLOBAL_STATE->DEVICE_CONFIG.board_version);
            ESP_LOGI(TAG, "ASIC: %dx %s (%d cores)", GLOBAL_STATE->DEVICE_CONFIG.family.asic_count, GLOBAL_STATE->DEVICE_CONFIG.family.asic.name, GLOBAL_STATE->DEVICE_CONFIG.family.asic.core_count);

            free(board_version);
            return ESP_OK;
        }
    }

    ESP_LOGI(TAG, "Custom Board Version: %s", board_version);

    GLOBAL_STATE->DEVICE_CONFIG.board_version = strdup(board_version);

    char * device_model = nvs_config_get_string(NVS_CONFIG_DEVICE_MODEL, "unknown");

    for (int i = 0 ; i < ARRAY_SIZE(default_families); i++) {
        if (strcasecmp(default_families[i].name, device_model) == 0) {
            GLOBAL_STATE->DEVICE_CONFIG.family = default_families[i];

            ESP_LOGI(TAG, "Device Model: %s", GLOBAL_STATE->DEVICE_CONFIG.family.name);

            break;
        }
    }

    char * asic_model = nvs_config_get_string(NVS_CONFIG_ASIC_MODEL, "unknown");

    for (int i = 0 ; i < ARRAY_SIZE(default_asic_configs); i++) {
        if (strcasecmp(default_asic_configs[i].name, asic_model) == 0) {
            GLOBAL_STATE->DEVICE_CONFIG.family.asic = default_asic_configs[i];

            ESP_LOGI(TAG, "ASIC: %dx %s (%d cores)", GLOBAL_STATE->DEVICE_CONFIG.family.asic_count, GLOBAL_STATE->DEVICE_CONFIG.family.asic.name, GLOBAL_STATE->DEVICE_CONFIG.family.asic.core_count);

            break;
        }
    }

    GLOBAL_STATE->DEVICE_CONFIG.plug_sense = nvs_config_get_u16(NVS_CONFIG_PLUG_SENSE, 0) != 0;
    GLOBAL_STATE->DEVICE_CONFIG.asic_enable = nvs_config_get_u16(NVS_CONFIG_ASIC_ENABLE, 0) != 0;
    GLOBAL_STATE->DEVICE_CONFIG.EMC2101 = nvs_config_get_u16(NVS_CONFIG_EMC2101, 0) != 0;
    GLOBAL_STATE->DEVICE_CONFIG.EMC2103 = nvs_config_get_u16(NVS_CONFIG_EMC2103, 0) != 0;
    GLOBAL_STATE->DEVICE_CONFIG.emc_internal_temp = nvs_config_get_u16(NVS_CONFIG_EMC_INTERNAL_TEMP, 0) != 0;
    GLOBAL_STATE->DEVICE_CONFIG.emc_ideality_factor = nvs_config_get_u16(NVS_CONFIG_EMC_IDEALITY_FACTOR, 0);
    GLOBAL_STATE->DEVICE_CONFIG.emc_beta_compensation = nvs_config_get_u16(NVS_CONFIG_EMC_BETA_COMPENSATION, 0);
    GLOBAL_STATE->DEVICE_CONFIG.emc_temp_offset = nvs_config_get_i32(NVS_CONFIG_EMC_TEMP_OFFSET, 0);
    GLOBAL_STATE->DEVICE_CONFIG.DS4432U = nvs_config_get_u16(NVS_CONFIG_DS4432U, 0) != 0;
    GLOBAL_STATE->DEVICE_CONFIG.INA260 = nvs_config_get_u16(NVS_CONFIG_INA260, 0) != 0;
    GLOBAL_STATE->DEVICE_CONFIG.TPS546 = nvs_config_get_u16(NVS_CONFIG_TPS546, 0) != 0;
    // test values
    GLOBAL_STATE->DEVICE_CONFIG.power_consumption_target = nvs_config_get_u16(NVS_CONFIG_POWER_CONSUMPTION_TARGET, 0);

    free(board_version);
    free(device_model);
    free(asic_model);

    return ESP_OK;
}
