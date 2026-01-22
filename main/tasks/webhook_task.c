#include "webhook_task.h"
#include "global_state.h"
#include "nvs_config.h"
#include "connect.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "vcore.h"
#include <string.h>
#include <sys/time.h>

static const char *TAG = "webhook_task";

// Helper function to create system info JSON (similar to GET_system_info but without httpd_req_t)
static cJSON* create_system_info_json(GlobalState *GLOBAL_STATE)
{
    char * ssid = nvs_config_get_string(NVS_CONFIG_WIFI_SSID);
    char * hostname = nvs_config_get_string(NVS_CONFIG_HOSTNAME);
    char * ipv4 = GLOBAL_STATE->SYSTEM_MODULE.ip_addr_str;
    char * ipv6 = GLOBAL_STATE->SYSTEM_MODULE.ipv6_addr_str;
    char * stratumURL = nvs_config_get_string(NVS_CONFIG_STRATUM_URL);
    char * fallbackStratumURL = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_URL);
    char * stratumUser = nvs_config_get_string(NVS_CONFIG_STRATUM_USER);
    char * fallbackStratumUser = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_USER);
    char * stratumCert = nvs_config_get_string(NVS_CONFIG_STRATUM_CERT);
    char * fallbackStratumCert = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_CERT);
    char * display = nvs_config_get_string(NVS_CONFIG_DISPLAY);
    float frequency = nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY);
    
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char formattedMac[18];
    snprintf(formattedMac, sizeof(formattedMac), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    int8_t wifi_rssi = -90;
    get_wifi_current_rssi(&wifi_rssi);

    cJSON * root = cJSON_CreateObject();
    cJSON_AddFloatToObject(root, "power", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.power);
    cJSON_AddFloatToObject(root, "voltage", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.voltage);
    cJSON_AddFloatToObject(root, "current", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.current);
    cJSON_AddFloatToObject(root, "temp", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.chip_temp_avg);
    cJSON_AddFloatToObject(root, "temp2", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.chip_temp2_avg);
    cJSON_AddFloatToObject(root, "vrTemp", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.vr_temp);
    cJSON_AddNumberToObject(root, "maxPower", GLOBAL_STATE->DEVICE_CONFIG.family.max_power);
    cJSON_AddNumberToObject(root, "nominalVoltage", GLOBAL_STATE->DEVICE_CONFIG.family.nominal_voltage);
    cJSON_AddFloatToObject(root, "hashRate", GLOBAL_STATE->SYSTEM_MODULE.current_hashrate);
    cJSON_AddFloatToObject(root, "hashRate_1m", GLOBAL_STATE->SYSTEM_MODULE.hashrate_1m);
    cJSON_AddFloatToObject(root, "hashRate_10m", GLOBAL_STATE->SYSTEM_MODULE.hashrate_10m);
    cJSON_AddFloatToObject(root, "hashRate_1h", GLOBAL_STATE->SYSTEM_MODULE.hashrate_1h);
    cJSON_AddFloatToObject(root, "expectedHashrate", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate);
    cJSON_AddFloatToObject(root, "errorPercentage", GLOBAL_STATE->SYSTEM_MODULE.error_percentage);
    cJSON_AddNumberToObject(root, "bestDiff", GLOBAL_STATE->SYSTEM_MODULE.best_nonce_diff);
    cJSON_AddNumberToObject(root, "bestSessionDiff", GLOBAL_STATE->SYSTEM_MODULE.best_session_nonce_diff);
    cJSON_AddNumberToObject(root, "poolDifficulty", GLOBAL_STATE->pool_difficulty);

    cJSON_AddNumberToObject(root, "isUsingFallbackStratum", GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback);
    cJSON_AddStringToObject(root, "poolConnectionInfo", GLOBAL_STATE->SYSTEM_MODULE.pool_connection_info);

    cJSON_AddNumberToObject(root, "isPSRAMAvailable", GLOBAL_STATE->psram_is_available);

    cJSON_AddNumberToObject(root, "freeHeap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "freeHeapInternal", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(root, "freeHeapSpiram", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    
    cJSON_AddNumberToObject(root, "coreVoltage", nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE));
    cJSON_AddNumberToObject(root, "coreVoltageActual", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.core_voltage);
    cJSON_AddNumberToObject(root, "frequency", frequency);
    cJSON_AddStringToObject(root, "ssid", ssid);
    cJSON_AddStringToObject(root, "macAddr", formattedMac);
    cJSON_AddStringToObject(root, "hostname", hostname);
    cJSON_AddStringToObject(root, "ipv4", ipv4);
    cJSON_AddStringToObject(root, "ipv6", ipv6);
    cJSON_AddStringToObject(root, "wifiStatus", GLOBAL_STATE->SYSTEM_MODULE.wifi_status);
    cJSON_AddNumberToObject(root, "wifiRSSI", wifi_rssi);
    cJSON_AddNumberToObject(root, "apEnabled", GLOBAL_STATE->SYSTEM_MODULE.ap_enabled);
    cJSON_AddNumberToObject(root, "sharesAccepted", GLOBAL_STATE->SYSTEM_MODULE.shares_accepted);
    cJSON_AddNumberToObject(root, "sharesRejected", GLOBAL_STATE->SYSTEM_MODULE.shares_rejected);

    cJSON *error_array = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "sharesRejectedReasons", error_array);
    
    for (int i = 0; i < GLOBAL_STATE->SYSTEM_MODULE.rejected_reason_stats_count; i++) {
        cJSON *error_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(error_obj, "message", GLOBAL_STATE->SYSTEM_MODULE.rejected_reason_stats[i].message);
        cJSON_AddNumberToObject(error_obj, "count", GLOBAL_STATE->SYSTEM_MODULE.rejected_reason_stats[i].count);
        cJSON_AddItemToArray(error_array, error_obj);
    }

    cJSON_AddNumberToObject(root, "uptimeSeconds", (esp_timer_get_time() - GLOBAL_STATE->SYSTEM_MODULE.start_time) / 1000000);
    cJSON_AddNumberToObject(root, "smallCoreCount", GLOBAL_STATE->DEVICE_CONFIG.family.asic.small_core_count);
    cJSON_AddStringToObject(root, "ASICModel", GLOBAL_STATE->DEVICE_CONFIG.family.asic.name);
    cJSON_AddStringToObject(root, "stratumURL", stratumURL);
    cJSON_AddNumberToObject(root, "stratumPort", nvs_config_get_u16(NVS_CONFIG_STRATUM_PORT));
    cJSON_AddStringToObject(root, "stratumUser", stratumUser);
    cJSON_AddNumberToObject(root, "stratumSuggestedDifficulty", nvs_config_get_u16(NVS_CONFIG_STRATUM_DIFFICULTY));
    cJSON_AddNumberToObject(root, "stratumExtranonceSubscribe", nvs_config_get_bool(NVS_CONFIG_STRATUM_EXTRANONCE_SUBSCRIBE));
    cJSON_AddNumberToObject(root, "stratumTLS", nvs_config_get_u16(NVS_CONFIG_STRATUM_TLS));
    cJSON_AddStringToObject(root, "stratumCert", stratumCert);
    cJSON_AddStringToObject(root, "fallbackStratumURL", fallbackStratumURL);
    cJSON_AddNumberToObject(root, "fallbackStratumPort", nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_PORT));
    cJSON_AddStringToObject(root, "fallbackStratumUser", fallbackStratumUser);
    cJSON_AddNumberToObject(root, "fallbackStratumSuggestedDifficulty", nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_DIFFICULTY));
    cJSON_AddNumberToObject(root, "fallbackStratumExtranonceSubscribe", nvs_config_get_bool(NVS_CONFIG_FALLBACK_STRATUM_EXTRANONCE_SUBSCRIBE));
    cJSON_AddNumberToObject(root, "fallbackStratumTLS", nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_TLS));
    cJSON_AddStringToObject(root, "fallbackStratumCert", fallbackStratumCert);
    cJSON_AddNumberToObject(root, "responseTime", GLOBAL_STATE->SYSTEM_MODULE.response_time);

    cJSON_AddStringToObject(root, "version", GLOBAL_STATE->SYSTEM_MODULE.version);
    cJSON_AddStringToObject(root, "axeOSVersion", GLOBAL_STATE->SYSTEM_MODULE.axeOSVersion);

    cJSON_AddStringToObject(root, "idfVersion", esp_get_idf_version());
    cJSON_AddStringToObject(root, "boardVersion", GLOBAL_STATE->DEVICE_CONFIG.board_version);
    cJSON_AddStringToObject(root, "resetReason", "N/A"); // Simplified for webhook
    cJSON_AddStringToObject(root, "runningPartition", esp_ota_get_running_partition()->label);

    cJSON_AddNumberToObject(root, "overheat_mode", nvs_config_get_bool(NVS_CONFIG_OVERHEAT_MODE));
    cJSON_AddNumberToObject(root, "overclockEnabled", nvs_config_get_bool(NVS_CONFIG_OVERCLOCK_ENABLED));
    cJSON_AddStringToObject(root, "display", display);
    cJSON_AddNumberToObject(root, "rotation", nvs_config_get_u16(NVS_CONFIG_ROTATION));
    cJSON_AddNumberToObject(root, "invertscreen", nvs_config_get_bool(NVS_CONFIG_INVERT_SCREEN));
    cJSON_AddNumberToObject(root, "displayTimeout", nvs_config_get_i32(NVS_CONFIG_DISPLAY_TIMEOUT));

    cJSON_AddNumberToObject(root, "autofanspeed", nvs_config_get_bool(NVS_CONFIG_AUTO_FAN_SPEED));
    cJSON_AddFloatToObject(root, "fanspeed", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.fan_perc);
    cJSON_AddNumberToObject(root, "manualFanSpeed", nvs_config_get_u16(NVS_CONFIG_MANUAL_FAN_SPEED));
    cJSON_AddNumberToObject(root, "minFanSpeed", nvs_config_get_u16(NVS_CONFIG_MIN_FAN_SPEED));
    cJSON_AddNumberToObject(root, "temptarget", nvs_config_get_u16(NVS_CONFIG_TEMP_TARGET));
    cJSON_AddNumberToObject(root, "fanrpm", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.fan_rpm);
    cJSON_AddNumberToObject(root, "fan2rpm", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.fan2_rpm);

    cJSON_AddNumberToObject(root, "statsFrequency", nvs_config_get_u16(NVS_CONFIG_STATISTICS_FREQUENCY));
    cJSON_AddNumberToObject(root, "blockFound", GLOBAL_STATE->SYSTEM_MODULE.block_found);

    if (GLOBAL_STATE->SYSTEM_MODULE.power_fault > 0) {
        cJSON_AddStringToObject(root, "power_fault", VCORE_get_fault_string(GLOBAL_STATE));
    }

    if (GLOBAL_STATE->block_height > 0) {
        cJSON_AddNumberToObject(root, "blockHeight", GLOBAL_STATE->block_height);
        cJSON_AddStringToObject(root, "scriptsig", GLOBAL_STATE->scriptsig);
        cJSON_AddNumberToObject(root, "networkDifficulty", GLOBAL_STATE->network_nonce_diff);
    }

    cJSON *hashrate_monitor = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "hashrateMonitor", hashrate_monitor);
    
    cJSON *asics_array = cJSON_CreateArray();
    cJSON_AddItemToObject(hashrate_monitor, "asics", asics_array);

    if (GLOBAL_STATE->HASHRATE_MONITOR_MODULE.is_initialized) {
        for (int asic_nr = 0; asic_nr < GLOBAL_STATE->DEVICE_CONFIG.family.asic_count; asic_nr++) {
            cJSON *asic = cJSON_CreateObject();
            cJSON_AddItemToArray(asics_array, asic);
            cJSON_AddFloatToObject(asic, "total", GLOBAL_STATE->HASHRATE_MONITOR_MODULE.total_measurement[asic_nr].hashrate);

            int hash_domains = GLOBAL_STATE->DEVICE_CONFIG.family.asic.hash_domains;
            cJSON* hash_domain_array = cJSON_CreateArray();
            for (int domain_nr = 0; domain_nr < hash_domains; domain_nr++) {
                cJSON *hashrate = cJSON_CreateFloat(GLOBAL_STATE->HASHRATE_MONITOR_MODULE.domain_measurements[asic_nr][domain_nr].hashrate);
                cJSON_AddItemToArray(hash_domain_array, hashrate);
            }
            cJSON_AddItemToObject(asic, "domains", hash_domain_array);
            cJSON_AddNumberToObject(asic, "errorCount", GLOBAL_STATE->HASHRATE_MONITOR_MODULE.error_measurement[asic_nr].value);
        }
    }

    // Add webhook config to response
    cJSON_AddNumberToObject(root, "webhookEnabled", nvs_config_get_bool(NVS_CONFIG_WEBHOOK_ENABLED));
    cJSON_AddStringToObject(root, "webhookUrl", nvs_config_get_string(NVS_CONFIG_WEBHOOK_URL));
    cJSON_AddNumberToObject(root, "webhookInterval", nvs_config_get_u16(NVS_CONFIG_WEBHOOK_INTERVAL));

    free(ssid);
    free(hostname);
    free(stratumURL);
    free(fallbackStratumURL);
    free(stratumCert);
    free(fallbackStratumCert);
    free(stratumUser);
    free(fallbackStratumUser);
    free(display);

    return root;
}

// HTTP event handler
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }
    return ESP_OK;
}

// Send webhook POST request
static esp_err_t send_webhook(GlobalState *GLOBAL_STATE, const char *url, const char *json_data)
{
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_data, strlen(json_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        int content_length = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG, "Webhook sent successfully, status=%d, content_length=%d", status_code, content_length);
    } else {
        ESP_LOGE(TAG, "Webhook send failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

// Check if WiFi is connected
static bool is_wifi_connected()
{
    wifi_ap_record_t ap_info;
    return (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
}

void webhook_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;

    ESP_LOGI(TAG, "Webhook task started");

    while (1) {
        // Check if webhook is enabled
        bool webhook_enabled = nvs_config_get_bool(NVS_CONFIG_WEBHOOK_ENABLED);
        
        if (!webhook_enabled) {
            vTaskDelay(10000 / portTICK_PERIOD_MS); // Check every 10 seconds if disabled
            continue;
        }

        // Get webhook URL and interval
        char *webhook_url = nvs_config_get_string(NVS_CONFIG_WEBHOOK_URL);
        uint16_t interval = nvs_config_get_u16(NVS_CONFIG_WEBHOOK_INTERVAL);

        // Validate URL
        if (webhook_url == NULL || strlen(webhook_url) == 0) {
            ESP_LOGW(TAG, "Webhook enabled but URL is empty");
            free(webhook_url);
            vTaskDelay(60000 / portTICK_PERIOD_MS); // Wait 1 minute before retry
            continue;
        }

        // Check WiFi connection
        if (!is_wifi_connected()) {
            ESP_LOGD(TAG, "WiFi not connected, skipping webhook");
            free(webhook_url);
            vTaskDelay(10000 / portTICK_PERIOD_MS); // Wait 10 seconds
            continue;
        }

        // Create system info JSON
        cJSON *json = create_system_info_json(GLOBAL_STATE);
        if (json == NULL) {
            ESP_LOGE(TAG, "Failed to create system info JSON");
            free(webhook_url);
            vTaskDelay(interval * 1000 / portTICK_PERIOD_MS);
            continue;
        }

        // Convert JSON to string
        char *json_string = cJSON_Print(json);
        if (json_string == NULL) {
            ESP_LOGE(TAG, "Failed to convert JSON to string");
            cJSON_Delete(json);
            free(webhook_url);
            vTaskDelay(interval * 1000 / portTICK_PERIOD_MS);
            continue;
        }

        // Send webhook
        ESP_LOGI(TAG, "Sending webhook to %s", webhook_url);
        send_webhook(GLOBAL_STATE, webhook_url, json_string);

        // Cleanup
        free(json_string);
        cJSON_Delete(json);
        free(webhook_url);

        // Wait for next interval
        vTaskDelay(interval * 1000 / portTICK_PERIOD_MS);
    }
}
