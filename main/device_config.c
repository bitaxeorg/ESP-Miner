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

    char * board_version_str = nvs_config_get_string(NVS_CONFIG_BOARD_VERSION, "000");
    int board_version = atoi(board_version_str);
    free(board_version_str);

    for (int i = 0 ; i < ARRAY_SIZE(default_configs); i++) {
        if (default_configs[i].board_version == board_version) {
            GLOBAL_STATE->DEVICE_CONFIG = default_configs[i];

            ESP_LOGI(TAG, "Device Model: %s", GLOBAL_STATE->DEVICE_CONFIG.family.name);
            ESP_LOGI(TAG, "Board Version: %d", GLOBAL_STATE->DEVICE_CONFIG.board_version);
            ESP_LOGI(TAG, "ASIC: %dx %s (%d cores)", GLOBAL_STATE->DEVICE_CONFIG.family.asic_count, GLOBAL_STATE->DEVICE_CONFIG.family.asic.name, GLOBAL_STATE->DEVICE_CONFIG.family.asic.core_count);

            // TODO: Read overrides from NVS

            return ESP_OK;
        }
    }

    ESP_LOGE(TAG, "Unknown board version %d", board_version);
    return ESP_FAIL;
}
