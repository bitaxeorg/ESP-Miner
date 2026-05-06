#include <stdio.h>
#include "esp_wifi.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "system_api_json.h"
#include "nvs_config.h"
#include "vcore.h"
#include "connect.h"
#include "hashrate_monitor_task.h"
#include "cjson_utils.h"
#include "statistics_task.h"

static const char *get_reset_reason_str(esp_reset_reason_t reason)
{
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

static void system_api_add_telemetry(cJSON *root, GlobalState *g) {
    if (!root || !g) return;

    // Power Group
    cJSON_AddFloatToObject(root, "power", g->POWER_MANAGEMENT_MODULE.power);
    cJSON_AddFloatToObject(root, "voltage", g->POWER_MANAGEMENT_MODULE.voltage);
    cJSON_AddFloatToObject(root, "current", g->POWER_MANAGEMENT_MODULE.current);
    cJSON_AddFloatToObject(root, "temp", g->POWER_MANAGEMENT_MODULE.chip_temp_avg);
    cJSON_AddFloatToObject(root, "temp2", g->POWER_MANAGEMENT_MODULE.chip_temp2_avg);
    cJSON_AddFloatToObject(root, "vrTemp", g->POWER_MANAGEMENT_MODULE.vr_temp);
    cJSON_AddFloatToObject(root, "coreVoltageActual", g->POWER_MANAGEMENT_MODULE.core_voltage);
    cJSON_AddFloatToObject(root, "actualFrequency", g->POWER_MANAGEMENT_MODULE.actual_frequency);
    cJSON_AddFloatToObject(root, "expectedHashrate", g->POWER_MANAGEMENT_MODULE.expected_hashrate);
    cJSON_AddNumberToObject(root, "fanspeed", g->POWER_MANAGEMENT_MODULE.fan_perc);
    cJSON_AddNumberToObject(root, "fanrpm", g->POWER_MANAGEMENT_MODULE.fan_rpm);
    cJSON_AddNumberToObject(root, "fan2rpm", g->POWER_MANAGEMENT_MODULE.fan2_rpm);

    // Hashrate / Mining Group
    cJSON_AddFloatToObject(root, "hashRate", g->SYSTEM_MODULE.current_hashrate);
    cJSON_AddFloatToObject(root, "hashRate_1m", g->SYSTEM_MODULE.hashrate_1m);
    cJSON_AddFloatToObject(root, "hashRate_10m", g->SYSTEM_MODULE.hashrate_10m);
    cJSON_AddFloatToObject(root, "hashRate_1h", g->SYSTEM_MODULE.hashrate_1h);
    cJSON_AddFloatToObject(root, "errorPercentage", g->SYSTEM_MODULE.error_percentage);
    cJSON_AddNumberToObject(root, "sharesAccepted", g->SYSTEM_MODULE.shares_accepted);
    cJSON_AddNumberToObject(root, "sharesRejected", g->SYSTEM_MODULE.shares_rejected);
    cJSON_AddNumberToObject(root, "bestDiff", g->SYSTEM_MODULE.best_nonce_diff);
    cJSON_AddNumberToObject(root, "bestSessionDiff", g->SYSTEM_MODULE.best_session_nonce_diff);
    cJSON_AddNumberToObject(root, "poolDifficulty", g->pool_difficulty);
    cJSON_AddFloatToObject(root, "responseTime", g->SYSTEM_MODULE.response_time);
    cJSON_AddFloatToObject(root, "processTime", g->SYSTEM_MODULE.process_time);

    // Dynamic Block Info
    cJSON_AddNumberToObject(root, "blockFound", g->SYSTEM_MODULE.block_found);
    cJSON_AddBoolToObject(root, "showNewBlock", g->SYSTEM_MODULE.show_new_block);
    if (g->block_height > 0) {
        cJSON_AddNumberToObject(root, "blockHeight", g->block_height);
        cJSON_AddStringToObject(root, "scriptsig", g->scriptsig);
        cJSON_AddNumberToObject(root, "networkDifficulty", g->network_nonce_diff);
        cJSON_AddNumberToObject(root, "coinbaseValueTotalSatoshis", g->coinbase_value_total_satoshis);
        cJSON_AddNumberToObject(root, "coinbaseValueUserSatoshis", g->coinbase_value_user_satoshis);
    }

    // Dynamic System Stats
    cJSON_AddNumberToObject(root, "freeHeap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "freeHeapInternal", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(root, "freeHeapSpiram", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(root, "uptimeSeconds", (uint32_t)((esp_timer_get_time() - g->SYSTEM_MODULE.start_time) / 1000000));
    cJSON_AddFloatToObject(root, "cpuUsage", g->SYSTEM_MODULE.cpu_usage);
    cJSON_AddBoolToObject(root, "miningPaused", g->SYSTEM_MODULE.mining_paused);
    cJSON_AddNumberToObject(root, "overheat_mode", g->SYSTEM_MODULE.overheat_mode ? 1 : 0);
    cJSON_AddStringToObject(root, "wifiStatus", g->SYSTEM_MODULE.wifi_status);

    int8_t rssi = -90;
    get_wifi_current_rssi(&rssi);
    cJSON_AddNumberToObject(root, "wifiRSSI", rssi);

    // Faults
    if (g->SYSTEM_MODULE.power_fault > 0) {
        cJSON_AddStringToObject(root, "power_fault", VCORE_get_fault_string(g));
    }
    if (g->SYSTEM_MODULE.hardware_fault) {
        cJSON_AddStringToObject(root, "hardware_fault", g->SYSTEM_MODULE.hardware_fault_msg);
    }
}

static void system_api_add_config(cJSON *root, GlobalState *g) {
    if (!root || !g) return;

    // Versions
    cJSON_AddStringToObject(root, "version", g->SYSTEM_MODULE.version ? g->SYSTEM_MODULE.version : "Unknown");
    cJSON_AddStringToObject(root, "axeOSVersion", g->SYSTEM_MODULE.axeOSVersion ? g->SYSTEM_MODULE.axeOSVersion : "Unknown");
    cJSON_AddStringToObject(root, "idfVersion", esp_get_idf_version());
    cJSON_AddStringToObject(root, "boardVersion", g->DEVICE_CONFIG.board_version ? g->DEVICE_CONFIG.board_version : "Unknown");

    // Hardware Details
    cJSON_AddNumberToObject(root, "maxPower", g->DEVICE_CONFIG.family.max_power);
    cJSON_AddNumberToObject(root, "nominalVoltage", g->DEVICE_CONFIG.family.nominal_voltage);
    cJSON_AddNumberToObject(root, "smallCoreCount", g->DEVICE_CONFIG.family.asic.small_core_count);
    cJSON_AddStringToObject(root, "ASICModel", g->DEVICE_CONFIG.family.asic.name ? g->DEVICE_CONFIG.family.asic.name : "Unknown");
    cJSON_AddNumberToObject(root, "isPSRAMAvailable", g->psram_is_available ? 1 : 0);
    cJSON_AddStringToObject(root, "resetReason", get_reset_reason_str(esp_reset_reason()));
    
    const esp_partition_t *running = esp_ota_get_running_partition();
    cJSON_AddStringToObject(root, "runningPartition", running ? running->label : "Unknown");

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char formattedMac[18];
    snprintf(formattedMac, sizeof(formattedMac), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(root, "macAddr", formattedMac);

    char *hostname = nvs_config_get_string(NVS_CONFIG_HOSTNAME);
    cJSON_AddStringToObject(root, "hostname", hostname ? hostname : "Unknown");
    free(hostname);

    // Network Config
    char *ssid = nvs_config_get_string(NVS_CONFIG_WIFI_SSID);
    cJSON_AddStringToObject(root, "ssid", ssid ? ssid : "Unknown");
    free(ssid);

    cJSON_AddStringToObject(root, "ipv4", g->SYSTEM_MODULE.ip_addr_str);
    cJSON_AddStringToObject(root, "ipv6", g->SYSTEM_MODULE.ipv6_addr_str);
    cJSON_AddNumberToObject(root, "apEnabled", g->SYSTEM_MODULE.ap_enabled ? 1 : 0);

    // Pool Configuration
    cJSON_AddStringToObject(root, "poolConnectionInfo", g->SYSTEM_MODULE.pool_connection_info);
    cJSON_AddNumberToObject(root, "isUsingFallbackStratum", g->SYSTEM_MODULE.is_using_fallback ? 1 : 0);
    
    char *s_url = nvs_config_get_string(NVS_CONFIG_STRATUM_URL);
    cJSON_AddStringToObject(root, "stratumURL", s_url ? s_url : "");
    free(s_url);
    cJSON_AddNumberToObject(root, "stratumPort", nvs_config_get_u16(NVS_CONFIG_STRATUM_PORT));
    char *s_user = nvs_config_get_string(NVS_CONFIG_STRATUM_USER);
    cJSON_AddStringToObject(root, "stratumUser", s_user ? s_user : "");
    free(s_user);
    cJSON_AddNumberToObject(root, "stratumSuggestedDifficulty", nvs_config_get_u16(NVS_CONFIG_STRATUM_DIFFICULTY));
    cJSON_AddBoolToObject(root, "stratumExtranonceSubscribe", nvs_config_get_bool(NVS_CONFIG_STRATUM_EXTRANONCE_SUBSCRIBE));
    cJSON_AddNumberToObject(root, "stratumTLS", nvs_config_get_u16(NVS_CONFIG_STRATUM_TLS));
    char *s_cert = nvs_config_get_string(NVS_CONFIG_STRATUM_CERT);
    cJSON_AddStringToObject(root, "stratumCert", s_cert ? s_cert : "");
    free(s_cert);
    cJSON_AddBoolToObject(root, "stratumDecodeCoinbase", nvs_config_get_bool(NVS_CONFIG_STRATUM_DECODE_COINBASE_TX));

    char *f_url = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_URL);
    cJSON_AddStringToObject(root, "fallbackStratumURL", f_url ? f_url : "");
    free(f_url);
    cJSON_AddNumberToObject(root, "fallbackStratumPort", nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_PORT));
    char *f_user = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_USER);
    cJSON_AddStringToObject(root, "fallbackStratumUser", f_user ? f_user : "");
    free(f_user);
    cJSON_AddNumberToObject(root, "fallbackStratumSuggestedDifficulty", nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_DIFFICULTY));
    cJSON_AddBoolToObject(root, "fallbackStratumExtranonceSubscribe", nvs_config_get_bool(NVS_CONFIG_FALLBACK_STRATUM_EXTRANONCE_SUBSCRIBE));
    cJSON_AddNumberToObject(root, "fallbackStratumTLS", nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_TLS));
    char *f_cert = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_CERT);
    cJSON_AddStringToObject(root, "fallbackStratumCert", f_cert ? f_cert : "");
    free(f_cert);
    cJSON_AddBoolToObject(root, "fallbackStratumDecodeCoinbase", nvs_config_get_bool(NVS_CONFIG_FALLBACK_STRATUM_DECODE_COINBASE_TX));

    // User Preferences
    cJSON_AddNumberToObject(root, "overclockEnabled", nvs_config_get_bool(NVS_CONFIG_OVERCLOCK_ENABLED) ? 1 : 0);
    char *disp_name = nvs_config_get_string(NVS_CONFIG_DISPLAY);
    cJSON_AddStringToObject(root, "display", disp_name ? disp_name : "");
    free(disp_name);
    cJSON_AddNumberToObject(root, "rotation", nvs_config_get_u16(NVS_CONFIG_ROTATION));
    cJSON_AddNumberToObject(root, "invertscreen", nvs_config_get_bool(NVS_CONFIG_INVERT_SCREEN) ? 1 : 0);
    cJSON_AddNumberToObject(root, "displayTimeout", nvs_config_get_i32(NVS_CONFIG_DISPLAY_TIMEOUT));
    cJSON_AddNumberToObject(root, "autofanspeed", nvs_config_get_bool(NVS_CONFIG_AUTO_FAN_SPEED) ? 1 : 0);
    cJSON_AddNumberToObject(root, "manualFanSpeed", nvs_config_get_u16(NVS_CONFIG_MANUAL_FAN_SPEED));
    cJSON_AddNumberToObject(root, "minFanSpeed", nvs_config_get_u16(NVS_CONFIG_MIN_FAN_SPEED));
    cJSON_AddNumberToObject(root, "temptarget", nvs_config_get_u16(NVS_CONFIG_TEMP_TARGET));
    cJSON_AddNumberToObject(root, "coreVoltage", nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE));
    cJSON_AddFloatToObject(root, "frequency", nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY));
    cJSON_AddNumberToObject(root, "statsFrequency", nvs_config_get_u16(NVS_CONFIG_STATISTICS_FREQUENCY));
    cJSON_AddNumberToObject(root, "statsLimit", MAX_STATISTICS_COUNT);
}

static void system_api_add_hashrate_monitor(cJSON *root, GlobalState *g) {
    if (!root || !g || !g->HASHRATE_MONITOR_MODULE.is_initialized) return;

    cJSON *monitor = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "hashrateMonitor", monitor);
    
    cJSON *asics = cJSON_CreateArray();
    cJSON_AddItemToObject(monitor, "asics", asics);

    int asic_count = g->DEVICE_CONFIG.family.asic_count;
    int hash_domains = g->DEVICE_CONFIG.family.asic.hash_domains;

    for (int i = 0; i < asic_count; i++) {
        cJSON *asic = cJSON_CreateObject();
        cJSON_AddItemToArray(asics, asic);
        
        cJSON_AddNumberToObject(asic, "total", g->HASHRATE_MONITOR_MODULE.total_measurement[i].hashrate);
        cJSON_AddNumberToObject(asic, "errorCount", (double)g->HASHRATE_MONITOR_MODULE.error_measurement[i].hashrate);
        
        cJSON *domains = cJSON_CreateArray();
        cJSON_AddItemToObject(asic, "domains", domains);
        for (int j = 0; j < hash_domains; j++) {
            cJSON_AddItemToArray(domains, cJSON_CreateNumber(g->HASHRATE_MONITOR_MODULE.domain_measurements[i][j].hashrate));
        }
    }
}

static void system_api_add_rejected_reasons(cJSON *root, GlobalState *g) {
    if (!root || !g) return;
    cJSON *rejected_reasons = cJSON_CreateArray();
    if (rejected_reasons) {
        cJSON_AddItemToObject(root, "sharesRejectedReasons", rejected_reasons);
        for (int i = 0; i < g->SYSTEM_MODULE.rejected_reason_stats_count; i++) {
            cJSON *obj = cJSON_CreateObject();
            if (obj) {
                cJSON_AddStringToObject(obj, "message", g->SYSTEM_MODULE.rejected_reason_stats[i].message);
                cJSON_AddNumberToObject(obj, "count", g->SYSTEM_MODULE.rejected_reason_stats[i].count);
                cJSON_AddItemToArray(rejected_reasons, obj);
            }
        }
    }
}

static void system_api_add_block_info(cJSON *root, GlobalState *g) {
    if (!root || !g || g->block_height <= 0) return;

    cJSON *signals = cJSON_CreateArray();
    if (signals) {
        for (int i = 0; i < g->block_signals_count; i++) {
            cJSON_AddItemToArray(signals, cJSON_CreateString(g->block_signals[i]));
        }
        cJSON_AddItemToObject(root, "blockSignals", signals);
    }

    cJSON *outputs = cJSON_CreateArray();
    if (outputs) {
        for (int i = 0; i < g->coinbase_output_count; i++) {
            cJSON *obj = cJSON_CreateObject();
            if (obj) {
                cJSON_AddNumberToObject(obj, "value", g->coinbase_outputs[i].value_satoshis);
                cJSON_AddStringToObject(obj, "address", g->coinbase_outputs[i].address);
                cJSON_AddItemToArray(outputs, obj);
            }
        }
        cJSON_AddItemToObject(root, "coinbaseOutputs", outputs);
    }
}

cJSON* system_api_get_full_json(GlobalState *g) {
    if (!g) return NULL;
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return NULL;

    system_api_add_telemetry(root, g);
    system_api_add_config(root, g);
    system_api_add_hashrate_monitor(root, g);

    // Arrays that involve global state loops (not simple addition)
    system_api_add_rejected_reasons(root, g);
    system_api_add_block_info(root, g);

    return root;
}
