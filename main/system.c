#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"

#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/inet.h"

#include "system.h"
#include "i2c_bitaxe.h"
#include "INA260.h"
#include "adc.h"
#include "connect.h"
#include "nvs_config.h"
#include "display.h"
#include "input.h"
#include "screen.h"
#include "vcore.h"
#include "thermal.h"
#include "utils.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

static const char * TAG = "system";

// Helper functions for float JSON
static const double FACTOR = 10000000.0;

static cJSON* cJSON_AddFloatToObject(cJSON * const object, const char * const name, const float number) {
    double d_value = round((double)number * FACTOR) / FACTOR;
    return cJSON_AddNumberToObject(object, name, d_value);
}

static cJSON* cJSON_CreateFloat(float number) {
    double d_value = round((double)number * FACTOR) / FACTOR;
    return cJSON_CreateNumber(d_value);
}

// Helper function to convert reset reason to string
static const char* esp_reset_reason_to_string(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN:    return "Reset reason can not be determined";
        case ESP_RST_POWERON:    return "Reset due to power-on event";
        case ESP_RST_EXT:        return "Reset by external pin (not applicable for ESP32)";
        case ESP_RST_SW:         return "Software reset via esp_restart";
        case ESP_RST_PANIC:      return "Software reset due to exception/panic";
        case ESP_RST_INT_WDT:    return "Reset (software or hardware) due to interrupt watchdog";
        case ESP_RST_TASK_WDT:   return "Reset due to task watchdog";
        case ESP_RST_WDT:        return "Reset due to other watchdogs";
        case ESP_RST_DEEPSLEEP:  return "Reset after exiting deep sleep mode";
        case ESP_RST_BROWNOUT:   return "Brownout reset (software or hardware)";
        case ESP_RST_SDIO:       return "Reset over SDIO";
        case ESP_RST_USB:        return "Reset by USB peripheral";
        case ESP_RST_JTAG:       return "Reset by JTAG";
        case ESP_RST_EFUSE:      return "Reset due to efuse error";
        case ESP_RST_PWR_GLITCH: return "Reset due to power glitch detected";
        case ESP_RST_CPU_LOCKUP: return "Reset due to CPU lock up (double exception)";
        default:                 return "Unknown reset";
    }
}

//local function prototypes
static esp_err_t ensure_overheat_mode_config();

void SYSTEM_init_system(GlobalState * GLOBAL_STATE)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    module->screen_page = 0;
    module->shares_accepted = 0;
    module->shares_rejected = 0;
    module->best_nonce_diff = nvs_config_get_u64(NVS_CONFIG_BEST_DIFF);
    module->best_session_nonce_diff = 0;
    module->start_time = esp_timer_get_time();
    module->lastClockSync = 0;
    module->block_found = false;
    
    // Initialize network address strings
    strcpy(module->ip_addr_str, "");
    strcpy(module->ipv6_addr_str, "");
    strcpy(module->wifi_status, "Initializing...");
    
    // set the pool url
    module->pool_url = nvs_config_get_string(NVS_CONFIG_STRATUM_URL);
    module->fallback_pool_url = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_URL);

    // set the pool port
    module->pool_port = nvs_config_get_u16(NVS_CONFIG_STRATUM_PORT);
    module->fallback_pool_port = nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_PORT);

    // set the pool tls
    module->pool_tls = nvs_config_get_u16(NVS_CONFIG_STRATUM_TLS);
    module->fallback_pool_tls = nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_TLS);

    // set the pool cert
    module->pool_cert = nvs_config_get_string(NVS_CONFIG_STRATUM_CERT);
    module->fallback_pool_cert = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_CERT);

    // set the pool user
    module->pool_user = nvs_config_get_string(NVS_CONFIG_STRATUM_USER);
    module->fallback_pool_user = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_USER);

    // set the pool password
    module->pool_pass = nvs_config_get_string(NVS_CONFIG_STRATUM_PASS);
    module->fallback_pool_pass = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_PASS);

    // set the pool difficulty
    module->pool_difficulty = nvs_config_get_u16(NVS_CONFIG_STRATUM_DIFFICULTY);
    module->fallback_pool_difficulty = nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_DIFFICULTY);

    // set the pool extranonce subscribe
    module->pool_extranonce_subscribe = nvs_config_get_bool(NVS_CONFIG_STRATUM_EXTRANONCE_SUBSCRIBE);
    module->fallback_pool_extranonce_subscribe = nvs_config_get_bool(NVS_CONFIG_FALLBACK_STRATUM_EXTRANONCE_SUBSCRIBE);

    // use fallback stratum
    module->use_fallback_stratum = nvs_config_get_bool(NVS_CONFIG_USE_FALLBACK_STRATUM);

    // set based on config
    module->is_using_fallback = module->use_fallback_stratum;

    // Initialize pool connection info
    strcpy(module->pool_connection_info, "Not Connected");

    // Initialize overheat_mode
    module->overheat_mode = nvs_config_get_bool(NVS_CONFIG_OVERHEAT_MODE);
    ESP_LOGI(TAG, "Initial overheat_mode value: %d", module->overheat_mode);

    //Initialize power_fault fault mode
    module->power_fault = 0;

    // set the best diff string
    suffixString(module->best_nonce_diff, module->best_diff_string, DIFF_STRING_SIZE, 0);
    suffixString(module->best_session_nonce_diff, module->best_session_diff_string, DIFF_STRING_SIZE, 0);

    // Initialize mutexes
    pthread_mutex_init(&GLOBAL_STATE->valid_jobs_lock, NULL);
}

void SYSTEM_init_versions(GlobalState * GLOBAL_STATE) {
    const esp_app_desc_t *app_desc = esp_app_get_description();
    
    // Store the firmware version
    GLOBAL_STATE->SYSTEM_MODULE.version = strdup(app_desc->version);
    if (GLOBAL_STATE->SYSTEM_MODULE.version == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for version");
        GLOBAL_STATE->SYSTEM_MODULE.version = strdup("Unknown");
    }
    
    // Read AxeOS version from SPIFFS
    FILE *f = fopen("/version.txt", "r");
    if (f == NULL) {
        ESP_LOGW(TAG, "Failed to open /version.txt");
        GLOBAL_STATE->SYSTEM_MODULE.axeOSVersion = strdup("Unknown");
    } else {
        char version[64];
        if (fgets(version, sizeof(version), f) == NULL) {
            ESP_LOGW(TAG, "Failed to read version from /version.txt");
            GLOBAL_STATE->SYSTEM_MODULE.axeOSVersion = strdup("Unknown");
        } else {
            // Remove trailing newline if present
            size_t len = strlen(version);
            if (len > 0 && version[len - 1] == '\n') {
                version[len - 1] = '\0';
            }
            GLOBAL_STATE->SYSTEM_MODULE.axeOSVersion = strdup(version);
            if (GLOBAL_STATE->SYSTEM_MODULE.axeOSVersion == NULL) {
                ESP_LOGE(TAG, "Failed to allocate memory for axeOSVersion");
                GLOBAL_STATE->SYSTEM_MODULE.axeOSVersion = strdup("Unknown");
            }
        }
        fclose(f);
    }
    
    ESP_LOGI(TAG, "Firmware Version: %s", GLOBAL_STATE->SYSTEM_MODULE.version);
    ESP_LOGI(TAG, "AxeOS Version: %s", GLOBAL_STATE->SYSTEM_MODULE.axeOSVersion);

    if (strcmp(GLOBAL_STATE->SYSTEM_MODULE.version, GLOBAL_STATE->SYSTEM_MODULE.axeOSVersion) != 0) {
        ESP_LOGE(TAG, "Firmware (%s) and AxeOS (%s) versions do not match. Please make sure to update both www.bin and esp-miner.bin.", 
            GLOBAL_STATE->SYSTEM_MODULE.version, 
            GLOBAL_STATE->SYSTEM_MODULE.axeOSVersion);
    }
}

esp_err_t SYSTEM_init_peripherals(GlobalState * GLOBAL_STATE) {
    
    ESP_RETURN_ON_ERROR(gpio_install_isr_service(0), TAG, "Error installing ISR service");

    // Initialize the core voltage regulator
    ESP_RETURN_ON_ERROR(VCORE_init(GLOBAL_STATE), TAG, "VCORE init failed!");

    ESP_RETURN_ON_ERROR(Thermal_init(&GLOBAL_STATE->DEVICE_CONFIG), TAG, "Thermal init failed!");

    vTaskDelay(500 / portTICK_PERIOD_MS);

    // Ensure overheat_mode config exists
    ESP_RETURN_ON_ERROR(ensure_overheat_mode_config(), TAG, "Failed to ensure overheat_mode config");

    ESP_RETURN_ON_ERROR(display_init(GLOBAL_STATE), TAG, "Display init failed!");

    ESP_RETURN_ON_ERROR(input_init(screen_button_press, toggle_wifi_softap), TAG, "Input init failed!");

    ESP_RETURN_ON_ERROR(screen_start(GLOBAL_STATE), TAG, "Screen start failed!");

    return ESP_OK;
}

void SYSTEM_notify_accepted_share(GlobalState * GLOBAL_STATE)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    module->shares_accepted++;
}

static int compare_rejected_reason_stats(const void *a, const void *b) {
    const RejectedReasonStat *ea = a;
    const RejectedReasonStat *eb = b;
    return (eb->count > ea->count) - (ea->count > eb->count);
}

void SYSTEM_notify_rejected_share(GlobalState * GLOBAL_STATE, char * error_msg)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    module->shares_rejected++;

    for (int i = 0; i < module->rejected_reason_stats_count; i++) {
        if (strncmp(module->rejected_reason_stats[i].message, error_msg, sizeof(module->rejected_reason_stats[i].message) - 1) == 0) {
            module->rejected_reason_stats[i].count++;
            return;
        }
    }

    if (module->rejected_reason_stats_count < sizeof(module->rejected_reason_stats)) {
        strncpy(module->rejected_reason_stats[module->rejected_reason_stats_count].message, 
                error_msg, 
                sizeof(module->rejected_reason_stats[module->rejected_reason_stats_count].message) - 1);
        module->rejected_reason_stats[module->rejected_reason_stats_count].message[sizeof(module->rejected_reason_stats[module->rejected_reason_stats_count].message) - 1] = '\0'; // Ensure null termination
        module->rejected_reason_stats[module->rejected_reason_stats_count].count = 1;
        module->rejected_reason_stats_count++;
    }

    if (module->rejected_reason_stats_count > 1) {
        qsort(module->rejected_reason_stats, module->rejected_reason_stats_count, 
            sizeof(module->rejected_reason_stats[0]), compare_rejected_reason_stats);
    }    
}

void SYSTEM_notify_new_ntime(GlobalState * GLOBAL_STATE, uint32_t ntime)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    // Hourly clock sync
    if (module->lastClockSync + (60 * 60) > ntime) {
        return;
    }
    ESP_LOGI(TAG, "Syncing clock");
    module->lastClockSync = ntime;
    struct timeval tv;
    tv.tv_sec = ntime;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
}

void SYSTEM_notify_found_nonce(GlobalState * GLOBAL_STATE, double diff, uint8_t job_id)
{
    SystemModule * module = &GLOBAL_STATE->SYSTEM_MODULE;

    if ((uint64_t) diff > module->best_session_nonce_diff) {
        module->best_session_nonce_diff = (uint64_t) diff;
        suffixString((uint64_t) diff, module->best_session_diff_string, DIFF_STRING_SIZE, 0);
    }

    double network_diff = networkDifficulty(GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[job_id]->target);
    if (diff >= network_diff) {
        module->block_found = true;
        ESP_LOGI(TAG, "FOUND BLOCK!!!!!!!!!!!!!!!!!!!!!! %f >= %f", diff, network_diff);
    }

    if ((uint64_t) diff <= module->best_nonce_diff) {
        return;
    }
    module->best_nonce_diff = (uint64_t) diff;

    nvs_config_set_u64(NVS_CONFIG_BEST_DIFF, module->best_nonce_diff);

    // make the best_nonce_diff into a string
    suffixString((uint64_t) diff, module->best_diff_string, DIFF_STRING_SIZE, 0);

    ESP_LOGI(TAG, "Network diff: %f", network_diff);
}

static esp_err_t ensure_overheat_mode_config() {
    bool overheat_mode = nvs_config_get_bool(NVS_CONFIG_OVERHEAT_MODE);

    ESP_LOGI(TAG, "Existing overheat_mode value: %d", overheat_mode);

    return ESP_OK;
}

cJSON* SYSTEM_create_info_json(GlobalState *GLOBAL_STATE)
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
    cJSON_AddStringToObject(root, "resetReason", esp_reset_reason_to_string(esp_reset_reason()));
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

    // Webhook configuration
    cJSON_AddNumberToObject(root, "webhookEnabled", nvs_config_get_bool(NVS_CONFIG_WEBHOOK_ENABLED));
    cJSON_AddStringToObject(root, "webhookUrl", nvs_config_get_string(NVS_CONFIG_WEBHOOK_URL));
    cJSON_AddNumberToObject(root, "webhookInterval", nvs_config_get_u16(NVS_CONFIG_WEBHOOK_INTERVAL));

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
