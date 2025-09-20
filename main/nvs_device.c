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

static const char *TAG = "nvs_device";

esp_err_t NVSDevice_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    
    nvs_stats_t stats;
    err = nvs_get_stats(NULL, &stats);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Used entries: %lu", stats.used_entries);
        ESP_LOGI(TAG, "Free entries: %lu", stats.free_entries);
        ESP_LOGI(TAG, "Available entries: %lu", stats.available_entries);
        ESP_LOGI(TAG, "Total entries: %lu", stats.total_entries);
    } else {
        ESP_LOGE(TAG, "Error getting NVS stats: %s\n", esp_err_to_name(err));
    }

    return err;
}
