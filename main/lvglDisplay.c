#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_config.h"
#include "i2c_bitaxe.h"
#include "global_state.h"

#define lvglDisplayI2CAddr 0x50
#define DISPLAY_UPDATE_INTERVAL_MS 5000

static i2c_master_dev_handle_t lvglDisplay_dev_handle;
static TickType_t lastUpdateTime = 0;

esp_err_t lvglDisplay_init(void) {
    lastUpdateTime = xTaskGetTickCount();
    return i2c_bitaxe_add_device(lvglDisplayI2CAddr, &lvglDisplay_dev_handle);
}

esp_err_t lvglUpdateDisplayValues(GlobalState *GLOBAL_STATE) 
{
    TickType_t currentTime = xTaskGetTickCount();
    
    // Only update if 5 seconds have passed
    if ((currentTime - lastUpdateTime) < pdMS_TO_TICKS(DISPLAY_UPDATE_INTERVAL_MS)) {
        return ESP_OK;
    }
    
    lastUpdateTime = currentTime;
    
    SystemModule *module = &GLOBAL_STATE->SYSTEM_MODULE;
    PowerManagementModule *power = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    uint8_t statusData[8];
    esp_err_t ret;
    
    // Pack hashrate (as fixed point, multiply by 100 to preserve 2 decimal places)
    uint32_t hashrateFixed = (uint32_t)(module->current_hashrate * 100);
    statusData[0] = (hashrateFixed >> 24) & 0xFF;
    statusData[1] = (hashrateFixed >> 16) & 0xFF;
    statusData[2] = (hashrateFixed >> 8) & 0xFF;
    statusData[3] = hashrateFixed & 0xFF;
    
    // Pack temperature (as fixed point, multiply by 10 to preserve 1 decimal place)
    uint16_t tempFixed = (uint16_t)(power->chip_temp_avg * 10);
    statusData[4] = (tempFixed >> 8) & 0xFF;
    statusData[5] = tempFixed & 0xFF;
    
    // Calculate efficiency (J/Th)
    float efficiency = power->power / (module->current_hashrate / 1000.0);
    uint16_t efficiencyFixed = (uint16_t)(efficiency * 10);
    statusData[6] = (efficiencyFixed >> 8) & 0xFF;
    statusData[7] = efficiencyFixed & 0xFF;
    
    // Send hashrate
    if ((ret = i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, statusData, 4)) != ESP_OK)
        return ret;
    
    // Send temperature and efficiency
    if ((ret = i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, &statusData[4], 4)) != ESP_OK)
        return ret;
    
    // Send shares count
    uint32_t shares = module->shares_accepted;
    statusData[0] = (shares >> 24) & 0xFF;
    statusData[1] = (shares >> 16) & 0xFF;
    statusData[2] = (shares >> 8) & 0xFF;
    statusData[3] = shares & 0xFF;
    
    return i2c_bitaxe_register_write_bytes(lvglDisplay_dev_handle, statusData, 4);
}


   httpd_resp_set_type(req, "application/json");

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

/*  //These are global variable available in the GLOBAL_STATE
    char * ssid = nvs_config_get_string(NVS_CONFIG_WIFI_SSID, CONFIG_ESP_WIFI_SSID);
    char * hostname = nvs_config_get_string(NVS_CONFIG_HOSTNAME, CONFIG_LWIP_LOCAL_HOSTNAME);
    uint8_t mac[6];
    char formattedMac[18];
    char * stratumURL = nvs_config_get_string(NVS_CONFIG_STRATUM_URL, CONFIG_STRATUM_URL);
    char * fallbackStratumURL = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_URL, CONFIG_FALLBACK_STRATUM_URL);
    char * stratumUser = nvs_config_get_string(NVS_CONFIG_STRATUM_USER, CONFIG_STRATUM_USER);
    char * fallbackStratumUser = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_USER, CONFIG_FALLBACK_STRATUM_USER);
    char * board_version = nvs_config_get_string(NVS_CONFIG_BOARD_VERSION, "unknown");

    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(formattedMac, 18, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        cJSON * root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "power", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.power);
    cJSON_AddNumberToObject(root, "voltage", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.voltage);
    cJSON_AddNumberToObject(root, "current", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.current);
    cJSON_AddNumberToObject(root, "temp", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.chip_temp_avg);
    cJSON_AddNumberToObject(root, "vrTemp", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.vr_temp);
    cJSON_AddNumberToObject(root, "hashRate", GLOBAL_STATE->SYSTEM_MODULE.current_hashrate);
    cJSON_AddStringToObject(root, "bestDiff", GLOBAL_STATE->SYSTEM_MODULE.best_diff_string);
    cJSON_AddStringToObject(root, "bestSessionDiff", GLOBAL_STATE->SYSTEM_MODULE.best_session_diff_string);

    cJSON_AddNumberToObject(root, "isUsingFallbackStratum", GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback);

    cJSON_AddNumberToObject(root, "freeHeap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "coreVoltage", nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, CONFIG_ASIC_VOLTAGE));
    cJSON_AddNumberToObject(root, "coreVoltageActual", VCORE_get_voltage_mv(GLOBAL_STATE));
    cJSON_AddNumberToObject(root, "frequency", nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, CONFIG_ASIC_FREQUENCY));
    cJSON_AddStringToObject(root, "ssid", ssid);
    cJSON_AddStringToObject(root, "macAddr", formattedMac);
    cJSON_AddStringToObject(root, "hostname", hostname);
    cJSON_AddStringToObject(root, "wifiStatus", GLOBAL_STATE->SYSTEM_MODULE.wifi_status);
    cJSON_AddNumberToObject(root, "sharesAccepted", GLOBAL_STATE->SYSTEM_MODULE.shares_accepted);
    cJSON_AddNumberToObject(root, "sharesRejected", GLOBAL_STATE->SYSTEM_MODULE.shares_rejected);
    cJSON_AddNumberToObject(root, "uptimeSeconds", (esp_timer_get_time() - GLOBAL_STATE->SYSTEM_MODULE.start_time) / 1000000);
    cJSON_AddNumberToObject(root, "asicCount", GLOBAL_STATE->asic_count);
    uint16_t small_core_count = 0;
    switch (GLOBAL_STATE->asic_model){
        case ASIC_BM1397:
            small_core_count = BM1397_SMALL_CORE_COUNT;
            break;
        case ASIC_BM1366:
            small_core_count = BM1366_SMALL_CORE_COUNT;
            break;
        case ASIC_BM1368:
            small_core_count = BM1368_SMALL_CORE_COUNT;
            break;
        case ASIC_BM1370:
            small_core_count = BM1370_SMALL_CORE_COUNT;
            break;
        case ASIC_UNKNOWN:
        default:
            small_core_count = -1;
            break;
    }
    cJSON_AddNumberToObject(root, "smallCoreCount", small_core_count);
    cJSON_AddStringToObject(root, "ASICModel", GLOBAL_STATE->asic_model_str);
    cJSON_AddStringToObject(root, "stratumURL", stratumURL);
    cJSON_AddStringToObject(root, "fallbackStratumURL", fallbackStratumURL);
    cJSON_AddNumberToObject(root, "stratumPort", nvs_config_get_u16(NVS_CONFIG_STRATUM_PORT, CONFIG_STRATUM_PORT));
    cJSON_AddNumberToObject(root, "fallbackStratumPort", nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_PORT, CONFIG_FALLBACK_STRATUM_PORT));
    cJSON_AddStringToObject(root, "stratumUser", stratumUser);
    cJSON_AddStringToObject(root, "fallbackStratumUser", fallbackStratumUser);

    cJSON_AddStringToObject(root, "version", esp_app_get_description()->version);
    cJSON_AddStringToObject(root, "boardVersion", board_version);
    cJSON_AddStringToObject(root, "runningPartition", esp_ota_get_running_partition()->label);

    cJSON_AddNumberToObject(root, "flipscreen", nvs_config_get_u16(NVS_CONFIG_FLIP_SCREEN, 1));
    cJSON_AddNumberToObject(root, "overheat_mode", nvs_config_get_u16(NVS_CONFIG_OVERHEAT_MODE, 0));
    cJSON_AddNumberToObject(root, "invertscreen", nvs_config_get_u16(NVS_CONFIG_INVERT_SCREEN, 0));

    cJSON_AddNumberToObject(root, "invertfanpolarity", nvs_config_get_u16(NVS_CONFIG_INVERT_FAN_POLARITY, 1));
    cJSON_AddNumberToObject(root, "autofanspeed", nvs_config_get_u16(NVS_CONFIG_AUTO_FAN_SPEED, 1));

    cJSON_AddNumberToObject(root, "fanspeed", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.fan_perc);
    cJSON_AddNumberToObject(root, "fanrpm", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.fan_rpm);

    if ((item = cJSON_GetObjectItem(root, "stratumURL")) != NULL) {
        nvs_config_set_string(NVS_CONFIG_STRATUM_URL, item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "fallbackStratumURL")) != NULL) {
        nvs_config_set_string(NVS_CONFIG_FALLBACK_STRATUM_URL, item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "stratumUser")) != NULL) {
        nvs_config_set_string(NVS_CONFIG_STRATUM_USER, item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "stratumPassword")) != NULL) {
        nvs_config_set_string(NVS_CONFIG_STRATUM_PASS, item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "fallbackStratumUser")) != NULL) {
        nvs_config_set_string(NVS_CONFIG_FALLBACK_STRATUM_USER, item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "fallbackStratumPassword")) != NULL) {
        nvs_config_set_string(NVS_CONFIG_FALLBACK_STRATUM_PASS, item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "stratumPort")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_STRATUM_PORT, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "fallbackStratumPort")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_FALLBACK_STRATUM_PORT, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "ssid")) != NULL) {
        nvs_config_set_string(NVS_CONFIG_WIFI_SSID, item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "wifiPass")) != NULL) {
        nvs_config_set_string(NVS_CONFIG_WIFI_PASS, item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "hostname")) != NULL) {
        nvs_config_set_string(NVS_CONFIG_HOSTNAME, item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "coreVoltage")) != NULL && item->valueint > 0) {
        nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "frequency")) != NULL && item->valueint > 0) {
        nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "flipscreen")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_FLIP_SCREEN, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "overheat_mode")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_OVERHEAT_MODE, 0);
    }
    if ((item = cJSON_GetObjectItem(root, "invertscreen")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_INVERT_SCREEN, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "invertfanpolarity")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_INVERT_FAN_POLARITY, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "autofanspeed")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_AUTO_FAN_SPEED, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "fanspeed")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_FAN_SPEED, item->valueint);
    }

    cJSON_Delete(root);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
*/