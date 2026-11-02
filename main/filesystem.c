#include "filesystem.h"
#include "global_state.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_vfs.h"

static const char * TAG = "filesystem";

esp_err_t filesystem_init(GlobalState * GLOBAL_STATE)
{
    // A factory image may contain an erased SPIFFS partition because AxeOS is
    // embedded in the application. Format that partition on first boot so the
    // factory self-test validates usable flash instead of failing on an empty
    // filesystem. Never auto-format during a manual self-test, where SPIFFS may
    // contain a user's custom AxeOS files.
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = GLOBAL_STATE->SELF_TEST_MODULE.is_factory,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ESP_FAIL;
    }

    GLOBAL_STATE->filesystem_is_available = true;

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        // This shouldn't happen
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    return ESP_OK;
}
