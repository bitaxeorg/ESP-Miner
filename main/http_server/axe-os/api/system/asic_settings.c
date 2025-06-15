#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_ota_ops.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "global_state.h"
#include "asic.h"

// static const char *TAG = "asic_settings";
static GlobalState *GLOBAL_STATE = NULL;

// Function declarations from http_server.c
extern esp_err_t is_network_allowed(httpd_req_t *req);
extern esp_err_t set_cors_headers(httpd_req_t *req);

// Initialize the ASIC API with the global state
void asic_api_init(GlobalState *global_state) {
    GLOBAL_STATE = global_state;
}

/* Handler for system board info endpoint, generally static */
esp_err_t GET_system_board(httpd_req_t *req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    httpd_resp_set_type(req, "application/json");

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char formattedMac[18];
    snprintf(formattedMac, sizeof(formattedMac), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    cJSON *root = cJSON_CreateObject();

    // Add ASIC model to the JSON object
    cJSON_AddStringToObject(root, "asicModel", GLOBAL_STATE->DEVICE_CONFIG.family.asic.name);
    cJSON_AddNumberToObject(root, "asicCount", GLOBAL_STATE->DEVICE_CONFIG.family.asic_count);
    cJSON_AddNumberToObject(root, "smallCoreCount", GLOBAL_STATE->DEVICE_CONFIG.family.asic.small_core_count);
    cJSON_AddStringToObject(root, "swarmColor", GLOBAL_STATE->DEVICE_CONFIG.family.swarm_color);
    cJSON_AddStringToObject(root, "deviceModel", GLOBAL_STATE->DEVICE_CONFIG.family.name);
    cJSON_AddStringToObject(root, "boardVersion", GLOBAL_STATE->DEVICE_CONFIG.board_version);
    cJSON_AddNumberToObject(root, "isPSRAMAvailable", GLOBAL_STATE->psram_is_available);
    cJSON_AddNumberToObject(root, "maxPower", GLOBAL_STATE->DEVICE_CONFIG.family.max_power);
    cJSON_AddNumberToObject(root, "nominalVoltage", GLOBAL_STATE->DEVICE_CONFIG.family.nominal_voltage);
    cJSON_AddNumberToObject(root, "apEnabled", GLOBAL_STATE->SYSTEM_MODULE.ap_enabled);
    cJSON_AddStringToObject(root, "macAddr", formattedMac);
    cJSON_AddStringToObject(root, "runningPartition", esp_ota_get_running_partition()->label);
    cJSON_AddStringToObject(root, "idfVersion", esp_get_idf_version());
    cJSON_AddStringToObject(root, "version", esp_app_get_description()->version);


    cJSON_AddNumberToObject(root, "defaultFrequency", GLOBAL_STATE->DEVICE_CONFIG.family.asic.default_frequency_mhz);

    // Create arrays for frequency and voltage options based on ASIC model
    cJSON *freqOptions = cJSON_CreateArray();
    size_t count = 0;
    while (GLOBAL_STATE->DEVICE_CONFIG.family.asic.frequency_options[count] != 0) {
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(GLOBAL_STATE->DEVICE_CONFIG.family.asic.frequency_options[count]));
        count++;
    }
    cJSON_AddItemToObject(root, "frequencyOptions", freqOptions);

    cJSON_AddNumberToObject(root, "defaultVoltage", GLOBAL_STATE->DEVICE_CONFIG.family.asic.default_voltage_mv);

    cJSON *voltageOptions = cJSON_CreateArray();
    count = 0;
    while (GLOBAL_STATE->DEVICE_CONFIG.family.asic.voltage_options[count] != 0) {
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(GLOBAL_STATE->DEVICE_CONFIG.family.asic.voltage_options[count]));
        count++;
    }
    cJSON_AddItemToObject(root, "voltageOptions", voltageOptions);

    const char *response = cJSON_PrintUnformatted(root);
    httpd_resp_sendstr(req, response);

    free((void *)response);
    cJSON_Delete(root);
    return ESP_OK;
}
