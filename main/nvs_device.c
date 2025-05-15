#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "nvs_flash.h"
#include "nvs_config.h"
#include "nvs_device.h"

#include "connect.h"
#include "global_state.h"

static const char * TAG = "nvs_device";

esp_err_t NVSDevice_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t NVSDevice_parse_config(GlobalState * GLOBAL_STATE) 
{
    GLOBAL_STATE->asic_model_str = nvs_config_get_string(NVS_CONFIG_ASIC_MODEL, "");
    GLOBAL_STATE->device_model_str = nvs_config_get_string(NVS_CONFIG_DEVICE_MODEL, "invalid");
    char * board_version = nvs_config_get_string(NVS_CONFIG_BOARD_VERSION, "000");
    GLOBAL_STATE->board_version = atoi(board_version);
    free(board_version);
    ESP_LOGI(TAG, "Found Device Model: %s", GLOBAL_STATE->device_model_str);
    ESP_LOGI(TAG, "Found Board Version: %d", GLOBAL_STATE->board_version);

    return ESP_OK;
}