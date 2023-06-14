#include "nvs_config.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

#define NVS_CONFIG_NAMESPACE "main"

static const char * TAG = "nvs_config";

char * nvs_config_get_string(const char * key, const char * default_value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Key %s not found in nvs, using default value", key);
        return strdup(default_value);
    }

    size_t size = 0;
    err = nvs_get_str(handle, key, NULL, &size);

    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Key %s not found in nvs, using default value", key);
        return strdup(default_value);
    }

    char * out = malloc(size);
    err = nvs_get_str(handle, key, out, &size);

    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Key %s not found in nvs, using default value", key);
        return strdup(default_value);
    }

    nvs_close(handle);
    return out;
}

uint16_t nvs_config_get_u16(const char * key, const uint16_t default_value)
{
    nvs_handle handle;
    esp_err_t err;
    err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Key %s not found in nvs, using default value", key);
        return default_value;
    }

    uint16_t out;
    err = nvs_get_u16(handle, key, &out);

    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Key %s not found in nvs, using default value", key);
        return default_value;
    }

    nvs_close(handle);
    return out;
}
