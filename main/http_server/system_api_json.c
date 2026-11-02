#include <stdio.h>
#include <string.h>
#include <math.h>
#include "esp_partition.h"
#include "esp_wifi.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "global_state.h"
#include "system_api_json.h"
#include "system.h"
#include "nvs_config.h"
#include "sv2_protocol.h"
#include "vcore.h"
#include "connect.h"
#include "hashrate_monitor_task.h"
#include "statistics_task.h"
#include "asic.h"
#include "yyjson_utils.h"

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

static void system_api_add_telemetry(yyjson_mut_doc *doc, yyjson_mut_val *root, GlobalState *g) {
    if (!doc || !root || !g) return;

    // Power Group
    yyjson_mut_obj_add_float(doc, root, "power", g->POWER_MANAGEMENT_MODULE.power);
    yyjson_mut_obj_add_float(doc, root, "voltage", g->POWER_MANAGEMENT_MODULE.voltage);
    yyjson_mut_obj_add_float(doc, root, "current", g->POWER_MANAGEMENT_MODULE.current);
    yyjson_mut_obj_add_float(doc, root, "temp", g->POWER_MANAGEMENT_MODULE.chip_temp_avg);
    yyjson_mut_obj_add_float(doc, root, "temp2", g->POWER_MANAGEMENT_MODULE.chip_temp2_avg);
    yyjson_mut_obj_add_float(doc, root, "vrTemp", g->POWER_MANAGEMENT_MODULE.vr_temp);
    yyjson_mut_obj_add_float(doc, root, "coreVoltageActual", g->POWER_MANAGEMENT_MODULE.core_voltage);
    yyjson_mut_obj_add_float(doc, root, "actualFrequency", g->POWER_MANAGEMENT_MODULE.actual_frequency);
    yyjson_mut_obj_add_float(doc, root, "expectedHashrate", g->POWER_MANAGEMENT_MODULE.expected_hashrate);
    yyjson_mut_obj_add_float(doc, root, "fanspeed", g->POWER_MANAGEMENT_MODULE.fan_perc);
    yyjson_mut_obj_add_uint(doc, root, "fanrpm", g->POWER_MANAGEMENT_MODULE.fan_rpm);
    yyjson_mut_obj_add_uint(doc, root, "fan2rpm", g->POWER_MANAGEMENT_MODULE.fan2_rpm);

    // Hashrate / Mining Group
    yyjson_mut_obj_add_float(doc, root, "hashRate", g->SYSTEM_MODULE.current_hashrate);
    yyjson_mut_obj_add_float(doc, root, "hashRate_1m", g->SYSTEM_MODULE.hashrate_1m);
    yyjson_mut_obj_add_float(doc, root, "hashRate_10m", g->SYSTEM_MODULE.hashrate_10m);
    yyjson_mut_obj_add_float(doc, root, "hashRate_1h", g->SYSTEM_MODULE.hashrate_1h);
    yyjson_mut_obj_add_float(doc, root, "errorPercentage", g->SYSTEM_MODULE.error_percentage);
    PoolSession *session = POOL_get_active_session(g);
    yyjson_mut_obj_add_uint(doc, root, "sharesAccepted", session ? session->shares_accepted : 0);
    yyjson_mut_obj_add_uint(doc, root, "sharesRejected", session ? session->shares_rejected : 0);
    yyjson_mut_obj_add_uint(doc, root, "sharesPending", session ? session->shares_pending : 0);
    yyjson_mut_obj_add_uint(doc, root, "bestDiff", g->SYSTEM_MODULE.best_nonce_diff);
    yyjson_mut_obj_add_uint(doc, root, "bestSessionDiff", g->SYSTEM_MODULE.best_session_nonce_diff);
    yyjson_mut_obj_add_double(doc, root, "poolDifficulty", session ? session->pool_difficulty : 0.0);
    yyjson_mut_obj_add_float(doc, root, "responseTime", session ? session->response_time : 0.0f);
    float rt_min = (session && session->response_time_min > 0.0f) ? session->response_time_min : (session ? session->last_response_time_min : 0.0f);
    float rt_max = (session && session->response_time_max > 0.0f) ? session->response_time_max : (session ? session->last_response_time_max : 0.0f);
    yyjson_mut_obj_add_float(doc, root, "responseTimeMin", rt_min > 0.0f ? rt_min : (session ? session->response_time : 0.0f));
    yyjson_mut_obj_add_float(doc, root, "responseTimeMax", rt_max > 0.0f ? rt_max : (session ? session->response_time : 0.0f));
    yyjson_mut_obj_add_uint(doc, root, "responseShareBatch", session ? session->response_share_batch : 0);
    yyjson_mut_obj_add_float(doc, root, "processTime", g->SYSTEM_MODULE.process_time);
    yyjson_mut_obj_add_uint(doc, root, "workReceived", session ? session->work_received : 0);

    // Dynamic Block Info
    yyjson_mut_obj_add_uint(doc, root, "blockFound", g->SYSTEM_MODULE.block_found);
    yyjson_mut_obj_add_bool(doc, root, "showNewBlock", g->SYSTEM_MODULE.show_new_block);
    yyjson_mut_obj_add_int(doc, root, "blockHeight", g->block_height);
    yyjson_mut_obj_add_strcpy(doc, root, "scriptsig", g->coinbase.scriptsig);
    yyjson_mut_obj_add_uint(doc, root, "networkDifficulty", g->network_nonce_diff);
    yyjson_mut_obj_add_uint(doc, root, "coinbaseValueTotalSatoshis", g->coinbase.total_value_satoshis);
    yyjson_mut_obj_add_uint(doc, root, "coinbaseValueUserSatoshis", g->coinbase.user_value_satoshis);
    yyjson_mut_obj_add_strcpy(doc, root, "coinbasePayoutStatus", coinbase_payout_status_to_string(g->coinbase.payout_status));

    // Dynamic System Stats
    yyjson_mut_obj_add_uint(doc, root, "freeHeap", esp_get_free_heap_size());
    yyjson_mut_obj_add_uint(doc, root, "freeHeapInternal", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    yyjson_mut_obj_add_uint(doc, root, "freeHeapSpiram", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    yyjson_mut_obj_add_uint(doc, root, "minFreeHeap", esp_get_minimum_free_heap_size());
    yyjson_mut_obj_add_uint(doc, root, "maxAllocHeap", heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    yyjson_mut_obj_add_uint(doc, root, "uptimeSeconds", g->SYSTEM_MODULE.uptime_seconds);
    yyjson_mut_obj_add_uint(doc, root, "totalUptimeSeconds", SYSTEM_noinit_get_total_uptime_seconds());
    yyjson_mut_obj_add_double(doc, root, "totalHashes", SYSTEM_noinit_get_total_hashes());
    yyjson_mut_obj_add_double(doc, root, "totalLog2Work", SYSTEM_noinit_get_total_log2_work());
    yyjson_mut_obj_add_float(doc, root, "cpuUsage", g->SYSTEM_MODULE.cpu_usage);
    yyjson_mut_obj_add_bool(doc, root, "miningPaused", g->SYSTEM_MODULE.mining_paused);
    yyjson_mut_obj_add_int(doc, root, "overheat_mode", g->SYSTEM_MODULE.overheat_mode ? 1 : 0);
    yyjson_mut_obj_add_strcpy(doc, root, "wifiStatus", g->SYSTEM_MODULE.wifi_status);

    int8_t rssi = -90;
    get_wifi_current_rssi(&rssi);
    yyjson_mut_obj_add_int(doc, root, "wifiRSSI", rssi);

    // Faults
    if (g->SYSTEM_MODULE.power_fault > 0) {
        const char *fault = VCORE_get_fault_string(g);
        if (fault) {
            yyjson_mut_obj_add_strcpy(doc, root, "power_fault", fault);
        }
    }
    if (g->SYSTEM_MODULE.hardware_fault) {
        yyjson_mut_obj_add_strcpy(doc, root, "hardware_fault", g->SYSTEM_MODULE.hardware_fault_msg);
    }
}

static void system_api_add_config(yyjson_mut_doc *doc, yyjson_mut_val *root, GlobalState *g) {
    if (!doc || !root || !g) return;

    // Versions
    yyjson_mut_obj_add_str(doc, root, "version", g->SYSTEM_MODULE.version ? g->SYSTEM_MODULE.version : "Unknown");
    yyjson_mut_obj_add_str(doc, root, "axeOSVersion", g->SYSTEM_MODULE.axeOSVersion ? g->SYSTEM_MODULE.axeOSVersion : "Unknown");
    yyjson_mut_obj_add_str(doc, root, "idfVersion", esp_get_idf_version());
    yyjson_mut_obj_add_str(doc, root, "boardVersion", g->DEVICE_CONFIG.board_version ? g->DEVICE_CONFIG.board_version : "Unknown");

    // Hardware Details
    yyjson_mut_obj_add_uint(doc, root, "maxPower", g->DEVICE_CONFIG.family.max_power);
    yyjson_mut_obj_add_uint(doc, root, "nominalVoltage", g->DEVICE_CONFIG.family.nominal_voltage);
    yyjson_mut_obj_add_uint(doc, root, "smallCoreCount", g->DEVICE_CONFIG.family.asic.small_core_count);
    yyjson_mut_obj_add_str(doc, root, "ASICModel", g->DEVICE_CONFIG.family.asic.name ? g->DEVICE_CONFIG.family.asic.name : "Unknown");
    yyjson_mut_obj_add_int(doc, root, "isPSRAMAvailable", g->psram_is_available ? 1 : 0);
    yyjson_mut_obj_add_str(doc, root, "resetReason", get_reset_reason_str(esp_reset_reason()));
    
    const esp_partition_t *running = esp_ota_get_running_partition();
    yyjson_mut_obj_add_str(doc, root, "runningPartition", running ? running->label : "Unknown");

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char formattedMac[18];
    snprintf(formattedMac, sizeof(formattedMac), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    yyjson_mut_obj_add_strcpy(doc, root, "macAddr", formattedMac);

    char *hostname = nvs_config_get_string(NVS_CONFIG_HOSTNAME);
    yyjson_mut_obj_add_strcpy(doc, root, "hostname", hostname ? hostname : "Unknown");
    free(hostname);

    // Network Config
    char *ssid = nvs_config_get_string(NVS_CONFIG_WIFI_SSID);
    yyjson_mut_obj_add_strcpy(doc, root, "ssid", ssid ? ssid : "Unknown");
    free(ssid);

    yyjson_mut_obj_add_strcpy(doc, root, "ipv4", g->SYSTEM_MODULE.ip_addr_str);
    yyjson_mut_obj_add_strcpy(doc, root, "ipv6", g->SYSTEM_MODULE.ipv6_addr_str);
    yyjson_mut_obj_add_int(doc, root, "apEnabled", g->SYSTEM_MODULE.ap_enabled ? 1 : 0);
    yyjson_mut_obj_add_int(doc, root, "useNTP", nvs_config_get_bool(NVS_CONFIG_USE_NTP) ? 1 : 0);

    // Pool Configuration
    PoolSession *session = POOL_get_active_session(g);
    const char *conn_info = (session && session->pool_connection_info[0] != '\0') 
        ? session->pool_connection_info 
        : "Not Connected";
    yyjson_mut_obj_add_strcpy(doc, root, "poolConnectionInfo", conn_info);

    uint16_t prim_idx = g->SYSTEM_MODULE.primary_pool_index;
    uint16_t sec_idx = g->SYSTEM_MODULE.secondary_pool_index;
    if (prim_idx >= MAX_POOLS) prim_idx = 0;
    if (sec_idx >= MAX_POOLS) sec_idx = 0;

    const char *prim_err = g->SYSTEM_MODULE.is_using_fallback 
        ? g->SYSTEM_MODULE.pool_sessions[prim_idx].pool_error 
        : "";
    yyjson_mut_obj_add_strcpy(doc, root, "primaryPoolError", prim_err);
    yyjson_mut_obj_add_strcpy(doc, root, "poolBanner", session ? session->pool_banner : "");
    yyjson_mut_obj_add_int(doc, root, "isUsingFallbackStratum", g->SYSTEM_MODULE.is_using_fallback ? 1 : 0);
    
    yyjson_mut_obj_add_uint(doc, root, "primaryPoolIndex", prim_idx);
    yyjson_mut_obj_add_uint(doc, root, "secondaryPoolIndex", sec_idx);
    yyjson_mut_obj_add_int(doc, root, "useFallbackStratum", g->SYSTEM_MODULE.use_fallback_stratum ? 1 : 0);

    yyjson_mut_val *pools_arr = yyjson_mut_obj_add_arr(doc, root, "pools");
    for (int i = 0; i < MAX_POOLS; i++) {
        PoolConfig *p = &g->SYSTEM_MODULE.pools[i];
        if (p->url && strlen(p->url) > 0) {
            yyjson_mut_val *p_obj = yyjson_mut_arr_add_obj(doc, pools_arr);
            yyjson_mut_obj_add_uint(doc, p_obj, "id", i);
            yyjson_mut_obj_add_str(doc, p_obj, "stratumProtocol", p->protocol == STRATUM_PROTOCOL_V2 ? STRATUM_V2 : STRATUM_V1);
            yyjson_mut_obj_add_strcpy(doc, p_obj, "stratumURL", p->url);
            yyjson_mut_obj_add_uint(doc, p_obj, "stratumPort", p->port);
            yyjson_mut_obj_add_strcpy(doc, p_obj, "stratumUser", p->user ? p->user : "");
            yyjson_mut_obj_add_str(doc, p_obj, "stratumPassword", "*****"); // hide password in GET response
            yyjson_mut_obj_add_uint(doc, p_obj, "stratumSuggestedDifficulty", p->difficulty);
            yyjson_mut_obj_add_bool(doc, p_obj, "stratumExtranonceSubscribe", p->extranonce_subscribe);
            yyjson_mut_obj_add_uint(doc, p_obj, "stratumTLS", p->tls);
            yyjson_mut_obj_add_strcpy(doc, p_obj, "stratumCert", p->cert ? p->cert : "");
            yyjson_mut_obj_add_bool(doc, p_obj, "stratumDecodeCoinbase", p->decode_coinbase_tx);
            yyjson_mut_obj_add_bool(doc, p_obj, "stratumShareWarning", p->share_warning);
            yyjson_mut_obj_add_str(doc, p_obj, "stratumV2ChannelType", sv2_channel_type_to_string(p->sv2_channel_type));
            yyjson_mut_obj_add_strcpy(doc, p_obj, "stratumV2AuthorityPubkey", p->sv2_authority_pubkey ? p->sv2_authority_pubkey : "");
            yyjson_mut_obj_add_bool(doc, p_obj, "stratumV2RequireAuth", p->sv2_require_auth);
        }
    }

    // Legacy fields for backwards compatibility
    PoolConfig *prim_pool = &g->SYSTEM_MODULE.pools[prim_idx];
    PoolConfig *sec_pool = &g->SYSTEM_MODULE.pools[sec_idx];

    yyjson_mut_obj_add_strcpy(doc, root, "stratumURL", prim_pool->url ? prim_pool->url : "");
    yyjson_mut_obj_add_uint(doc, root, "stratumPort", prim_pool->port);
    yyjson_mut_obj_add_strcpy(doc, root, "stratumUser", prim_pool->user ? prim_pool->user : "");
    yyjson_mut_obj_add_uint(doc, root, "stratumSuggestedDifficulty", prim_pool->difficulty);
    yyjson_mut_obj_add_bool(doc, root, "stratumExtranonceSubscribe", prim_pool->extranonce_subscribe);
    yyjson_mut_obj_add_uint(doc, root, "stratumTLS", prim_pool->tls);
    yyjson_mut_obj_add_strcpy(doc, root, "stratumCert", prim_pool->cert ? prim_pool->cert : "");
    yyjson_mut_obj_add_bool(doc, root, "stratumDecodeCoinbase", prim_pool->decode_coinbase_tx);
    yyjson_mut_obj_add_bool(doc, root, "stratumShareWarning", prim_pool->share_warning);
    yyjson_mut_obj_add_str(doc, root, "stratumProtocol", prim_pool->protocol == STRATUM_PROTOCOL_V2 ? STRATUM_V2 : STRATUM_V1);
    yyjson_mut_obj_add_strcpy(doc, root, "stratumV2AuthorityPubkey", prim_pool->sv2_authority_pubkey ? prim_pool->sv2_authority_pubkey : "");
    yyjson_mut_obj_add_str(doc, root, "stratumV2ChannelType", sv2_channel_type_to_string(prim_pool->sv2_channel_type));

    yyjson_mut_obj_add_strcpy(doc, root, "fallbackStratumURL", sec_pool->url ? sec_pool->url : "");
    yyjson_mut_obj_add_uint(doc, root, "fallbackStratumPort", sec_pool->port);
    yyjson_mut_obj_add_strcpy(doc, root, "fallbackStratumUser", sec_pool->user ? sec_pool->user : "");
    yyjson_mut_obj_add_uint(doc, root, "fallbackStratumSuggestedDifficulty", sec_pool->difficulty);
    yyjson_mut_obj_add_bool(doc, root, "fallbackStratumExtranonceSubscribe", sec_pool->extranonce_subscribe);
    yyjson_mut_obj_add_uint(doc, root, "fallbackStratumTLS", sec_pool->tls);
    yyjson_mut_obj_add_strcpy(doc, root, "fallbackStratumCert", sec_pool->cert ? sec_pool->cert : "");
    yyjson_mut_obj_add_bool(doc, root, "fallbackStratumDecodeCoinbase", sec_pool->decode_coinbase_tx);
    yyjson_mut_obj_add_bool(doc, root, "fallbackStratumShareWarning", sec_pool->share_warning);
    yyjson_mut_obj_add_str(doc, root, "fallbackStratumProtocol", sec_pool->protocol == STRATUM_PROTOCOL_V2 ? STRATUM_V2 : STRATUM_V1);
    yyjson_mut_obj_add_strcpy(doc, root, "fallbackStratumV2AuthorityPubkey", sec_pool->sv2_authority_pubkey ? sec_pool->sv2_authority_pubkey : "");
    yyjson_mut_obj_add_str(doc, root, "fallbackStratumV2ChannelType", sv2_channel_type_to_string(sec_pool->sv2_channel_type));

    // User Preferences
    yyjson_mut_obj_add_int(doc, root, "useCustomWWW", nvs_config_get_bool(NVS_CONFIG_USE_CUSTOM_WWW) ? 1 : 0);
    yyjson_mut_obj_add_int(doc, root, "overclockEnabled", nvs_config_get_bool(NVS_CONFIG_OVERCLOCK_ENABLED) ? 1 : 0);
    char *disp_name = nvs_config_get_string(NVS_CONFIG_DISPLAY);
    yyjson_mut_obj_add_strcpy(doc, root, "display", disp_name ? disp_name : "");
    free(disp_name);
    yyjson_mut_obj_add_uint(doc, root, "rotation", nvs_config_get_u16(NVS_CONFIG_ROTATION));
    yyjson_mut_obj_add_int(doc, root, "invertscreen", nvs_config_get_bool(NVS_CONFIG_INVERT_SCREEN) ? 1 : 0);
    yyjson_mut_obj_add_int(doc, root, "displayTimeout", nvs_config_get_i32(NVS_CONFIG_DISPLAY_TIMEOUT));
    yyjson_mut_obj_add_int(doc, root, "autofanspeed", nvs_config_get_bool(NVS_CONFIG_AUTO_FAN_SPEED) ? 1 : 0);
    yyjson_mut_obj_add_uint(doc, root, "manualFanSpeed", nvs_config_get_u16(NVS_CONFIG_MANUAL_FAN_SPEED));
    yyjson_mut_obj_add_uint(doc, root, "minFanSpeed", nvs_config_get_u16(NVS_CONFIG_MIN_FAN_SPEED));
    yyjson_mut_obj_add_uint(doc, root, "temptarget", nvs_config_get_u16(NVS_CONFIG_TEMP_TARGET));
    yyjson_mut_obj_add_uint(doc, root, "coreVoltage", nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE));
    yyjson_mut_obj_add_float(doc, root, "frequency", nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY));
    yyjson_mut_obj_add_uint(doc, root, "statsFrequency", nvs_config_get_u16(NVS_CONFIG_STATISTICS_FREQUENCY));
    yyjson_mut_obj_add_uint(doc, root, "statsLimit", MAX_STATISTICS_COUNT);
}

static void system_api_add_hashrate_monitor(yyjson_mut_doc *doc, yyjson_mut_val *root, GlobalState *g) {
    if (!doc || !root || !g || !g->HASHRATE_MONITOR_MODULE.is_initialized) return;

    yyjson_mut_val *monitor = yyjson_mut_obj_add_obj(doc, root, "hashrateMonitor");
    yyjson_mut_val *asics = yyjson_mut_obj_add_arr(doc, monitor, "asics");

    int asic_count = g->DEVICE_CONFIG.family.asic_count;
    int hash_domains = g->DEVICE_CONFIG.family.asic.hash_domains;

    for (int i = 0; i < asic_count; i++) {
        yyjson_mut_val *asic = yyjson_mut_arr_add_obj(doc, asics);
        yyjson_mut_obj_add_float(doc, asic, "total", g->HASHRATE_MONITOR_MODULE.total_measurement[i].hashrate);
        yyjson_mut_obj_add_uint(doc, asic, "errorCount", g->HASHRATE_MONITOR_MODULE.error_measurement[i].value);
        yyjson_mut_val *domains = yyjson_mut_obj_add_arr(doc, asic, "domains");
        for (int j = 0; j < hash_domains; j++) {
            asic_domain_measurement_t measurement = {0};
            ASIC_get_domain_measurement(g, i, j, &measurement);
            yyjson_mut_arr_add_float(doc, domains, measurement.hashrate);
        }
    }
}

static void system_api_add_rejected_reasons(yyjson_mut_doc *doc, yyjson_mut_val *root, GlobalState *g) {
    if (!doc || !root || !g) return;
    yyjson_mut_val *rejected_reasons = yyjson_mut_obj_add_arr(doc, root, "sharesRejectedReasons");
    PoolSession *session = POOL_get_active_session(g);
    if (!session) return;
    for (int i = 0; i < session->rejected_reason_stats_count; i++) {
        yyjson_mut_val *obj = yyjson_mut_arr_add_obj(doc, rejected_reasons);
        yyjson_mut_obj_add_strcpy(doc, obj, "message", session->rejected_reason_stats[i].message);
        yyjson_mut_obj_add_uint(doc, obj, "count", session->rejected_reason_stats[i].count);
    }
}

static void system_api_add_block_info(yyjson_mut_doc *doc, yyjson_mut_val *root, GlobalState *g) {
    if (!doc || !root || !g) return;

    yyjson_mut_val *signals = yyjson_mut_obj_add_arr(doc, root, "blockSignals");
    for (int i = 0; i < g->block_signals_count; i++) {
        yyjson_mut_arr_add_strcpy(doc, signals, g->block_signals[i]);
    }

    yyjson_mut_val *outputs = yyjson_mut_obj_add_arr(doc, root, "coinbaseOutputs");
    int count = g->coinbase.output_count;
    if (count > MAX_COINBASE_TX_OUTPUTS) {
        count = MAX_COINBASE_TX_OUTPUTS;
    } else if (count < 0) {
        count = 0;
    }
    for (int i = 0; i < count; i++) {
        yyjson_mut_val *obj = yyjson_mut_arr_add_obj(doc, outputs);
        yyjson_mut_obj_add_uint(doc, obj, "value", g->coinbase.outputs[i].value_satoshis);
        yyjson_mut_obj_add_strcpy(doc, obj, "address", g->coinbase.outputs[i].address);
        yyjson_mut_obj_add_int(doc, obj, "isUserOutput", g->coinbase.outputs[i].is_user_output ? 1 : 0);
    }

    yyjson_mut_obj_add_int(doc, root, "coinbaseOthersCount", g->coinbase.others_count);
    yyjson_mut_obj_add_uint(doc, root, "coinbaseOthersValueSatoshis", g->coinbase.others_value_satoshis);
}

static void system_api_add_partitions(yyjson_mut_doc *doc, yyjson_mut_val *root, GlobalState * GLOBAL_STATE) {
    if (!doc || !root || !GLOBAL_STATE) return;

    yyjson_mut_val *partitions_array = yyjson_mut_obj_add_arr(doc, root, "partitions");

    for (int i = 0; i < GLOBAL_STATE->SYSTEM_MODULE.cached_partitions_count; i++) {
        cached_partition_t *cp = &GLOBAL_STATE->SYSTEM_MODULE.cached_partitions[i];
        if (cp->part == NULL) continue;

        yyjson_mut_val *p_obj = yyjson_mut_arr_add_obj(doc, partitions_array);
        yyjson_mut_obj_add_strcpy(doc, p_obj, "label", cp->part->label);
        yyjson_mut_obj_add_bool(doc, p_obj, "isCurrent", cp->isCurrent);
        yyjson_mut_obj_add_bool(doc, p_obj, "isFactory", cp->part->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY);

        if (cp->version[0] != '\0') {
            yyjson_mut_obj_add_strcpy(doc, p_obj, "version", cp->version);
            yyjson_mut_obj_add_strcpy(doc, p_obj, "compileDate", cp->compileDate);
            yyjson_mut_obj_add_strcpy(doc, p_obj, "compileTime", cp->compileTime);
            if (cp->usagePercent >= 0) {
                yyjson_mut_obj_add_int(doc, p_obj, "usagePercent", cp->usagePercent);
            } else {
                yyjson_mut_obj_add_null(doc, p_obj, "usagePercent");
            }
        } else {
            yyjson_mut_obj_add_null(doc, p_obj, "version");
            yyjson_mut_obj_add_null(doc, p_obj, "compileDate");
            yyjson_mut_obj_add_null(doc, p_obj, "compileTime");
            yyjson_mut_obj_add_null(doc, p_obj, "usagePercent");
        }
    }
}

yyjson_mut_doc* system_api_get_full_doc(GlobalState * GLOBAL_STATE, const yyjson_alc *alc) {
    if (!GLOBAL_STATE) return NULL;
    yyjson_mut_doc *doc = yyjson_mut_doc_new(alc);
    if (doc == NULL) return NULL;
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    system_api_add_telemetry(doc, root, GLOBAL_STATE);
    system_api_add_config(doc, root, GLOBAL_STATE);
    system_api_add_hashrate_monitor(doc, root, GLOBAL_STATE);
    system_api_add_partitions(doc, root, GLOBAL_STATE);
    system_api_add_rejected_reasons(doc, root, GLOBAL_STATE);
    system_api_add_block_info(doc, root, GLOBAL_STATE);

    return doc;
}
