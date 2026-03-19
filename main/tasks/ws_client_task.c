/**
 * ws_client_task.c — thin wrapper around gateway_core for ESP32/BitAxe
 *
 * Responsibilities of this file:
 *   1. Hold the binary-patchable embedded credentials
 *   2. Extract credentials (embedded or NVS fallback) and call gateway_core_init()
 *   3. Run a periodic esp_timer to push self-stats into the gateway platform cache
 *   4. Call gateway_core_run() — blocks indefinitely, reconnects automatically
 *
 * All protocol logic (tRPC, command dispatch, miner polling) lives in
 * gateway/src/gateway_core.c and gateway/src/miner_adapter.c.
 */

#include "ws_client_task.h"
#include "gateway_task.h"
#include "global_state.h"
#include "nvs_config.h"
#include "gateway_core.h"
#include "gateway_types.h"
#include "gateway_platform_esp32.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "ws_client";

// ============================================================
// Embedded credentials — binary-patchable at download time.
//
// Format: "HASHLY_CID:" + value + null padding to fill the field.
// The patcher replaces the entire array contents.
//
// __attribute__((used)) prevents dead-code elimination.
// Non-const (writable .data section) prevents GCC from merging
// these into .rodata, which would break binary patching.
// ============================================================

#define CREDENTIAL_LEN 128
#define URL_LEN        256

static volatile char __attribute__((used))
    EMBEDDED_CLIENT_ID[CREDENTIAL_LEN]     = "HASHLY_CID:__PLACEHOLDER_CLIENT_ID__";

static volatile char __attribute__((used))
    EMBEDDED_CLIENT_SECRET[CREDENTIAL_LEN] = "HASHLY_SEC:__PLACEHOLDER_CLIENT_SECRET__";

static volatile char __attribute__((used))
    EMBEDDED_WS_URL[URL_LEN]               = "HASHLY_URL:__PLACEHOLDER_WS_URL__";

// Extract the value after the sentinel prefix
static const char *get_credential(volatile const char *raw, const char *prefix)
{
    const char *str       = (const char *)raw;
    size_t      prefix_len = strlen(prefix);
    if (strncmp(str, prefix, prefix_len) == 0) {
        return str + prefix_len;
    }
    return str;  // already patched with raw value
}

// ============================================================
// Public accessors (used by HTTP API, screen, etc.)
// ============================================================

bool        ws_client_is_connected(void)    { return gateway_core_is_connected(); }
bool        ws_client_is_authenticated(void) { return gateway_core_is_authenticated(); }
const char *ws_client_get_client_id(void)   { return get_credential(EMBEDDED_CLIENT_ID, "HASHLY_CID:"); }

// ============================================================
// Self-stats push — periodic timer (runs in timer task context)
// ============================================================

static GlobalState *g_state = NULL;

static void build_self_stats(PeerMinerState *out)
{
    memset(out, 0, sizeof(*out));
    if (!g_state) return;

    SystemModule        *sys = &g_state->SYSTEM_MODULE;
    PowerManagementModule *pm = &g_state->POWER_MANAGEMENT_MODULE;

    // Identity
    strncpy(out->ip, sys->ip_addr_str, sizeof(out->ip) - 1);
    out->type   = MINER_TYPE_BITAXE;
    out->status = PEER_STATUS_ONLINE;

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out->mac_addr, sizeof(out->mac_addr),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // NVS: hostname, ASIC model, device model
    char *hostname = nvs_config_get_string(NVS_CONFIG_HOSTNAME);
    if (hostname) {
        strncpy(out->hostname, hostname, sizeof(out->hostname) - 1);
        free(hostname);
    }
    char *asic_model = nvs_config_get_string(NVS_CONFIG_ASIC_MODEL);
    if (asic_model) {
        strncpy(out->asic_model, asic_model, sizeof(out->asic_model) - 1);
        free(asic_model);
    }
    char *device_model = nvs_config_get_string(NVS_CONFIG_DEVICE_MODEL);
    if (device_model) {
        strncpy(out->device_model, device_model, sizeof(out->device_model) - 1);
        free(device_model);
    }

    // Performance
    out->hashrate        = sys->hashrate_1m;
    out->shares_accepted = sys->shares_accepted;
    out->shares_rejected = sys->shares_rejected;
    out->found_blocks    = sys->block_found;
    out->best_diff       = (double)sys->best_nonce_diff;
    out->best_session_diff = (double)sys->best_session_nonce_diff;
    out->pool_difficulty = (double)sys->pool_difficulty;

    // Thermal / electrical
    out->temp      = pm->chip_temp_avg;
    out->temp_avg  = pm->chip_temp_avg;
    out->temp_max  = (pm->chip_temp2_avg > pm->chip_temp_avg)
                        ? pm->chip_temp2_avg : pm->chip_temp_avg;
    out->power     = pm->power;
    out->voltage   = pm->voltage;
    out->current   = pm->current;
    out->vr_temp   = pm->vr_temp;
    out->fan_pct   = (int)pm->fan_perc;
    out->fan_rpm   = (pm->fan2_rpm > 0)
                        ? (int)((pm->fan_rpm + pm->fan2_rpm) / 2)
                        : (int)pm->fan_rpm;
    out->frequency = pm->frequency_value;

    // Uptime
    if (sys->start_time > 0) {
        int64_t elapsed_us = esp_timer_get_time() - sys->start_time;
        if (elapsed_us > 0) {
            out->uptime_seconds = (uint32_t)(elapsed_us / 1000000LL);
        }
    }

    // Primary pool
    if (sys->pool_url && *sys->pool_url) {
        snprintf(out->pools[0].url, sizeof(out->pools[0].url),
                 "%s:%u", sys->pool_url, sys->pool_port);
        if (sys->pool_user)
            strncpy(out->pools[0].user, sys->pool_user, sizeof(out->pools[0].user) - 1);
        out->pools[0].active       = !sys->use_fallback_stratum;
        out->pools[0].accepted     = sys->shares_accepted;
        out->pools[0].rejected     = sys->shares_rejected;
        out->pools[0].stratum_diff = sys->pool_difficulty;
        out->pool_count = 1;

        if (sys->fallback_pool_url && *sys->fallback_pool_url) {
            snprintf(out->pools[1].url, sizeof(out->pools[1].url),
                     "%s:%u", sys->fallback_pool_url, sys->fallback_pool_port);
            if (sys->fallback_pool_user)
                strncpy(out->pools[1].user, sys->fallback_pool_user,
                        sizeof(out->pools[1].user) - 1);
            out->pools[1].active       = sys->use_fallback_stratum;
            out->pools[1].stratum_diff = sys->fallback_pool_difficulty;
            out->pool_count = 2;
        }
    }

    out->last_seen = esp_timer_get_time() / 1000LL;  // ms
}

static void self_stats_timer_cb(void *arg)
{
    (void)arg;
    PeerMinerState stats;
    build_self_stats(&stats);
    platform_esp32_update_self_stats(&stats);
}

// ============================================================
// FreeRTOS task entry point
// ============================================================

void ws_client_task(void *pvParameters)
{
    g_state = (GlobalState *)pvParameters;

    ESP_LOGI(TAG, "WebSocket client task started");

    // ── Resolve credentials ──────────────────────────────────────────────────
    const char *client_id     = get_credential(EMBEDDED_CLIENT_ID,     "HASHLY_CID:");
    const char *client_secret = get_credential(EMBEDDED_CLIENT_SECRET, "HASHLY_SEC:");
    const char *ws_url        = get_credential(EMBEDDED_WS_URL,        "HASHLY_URL:");

    // Fall back to NVS when running with placeholder firmware (dev builds)
    if (strstr(ws_url, "__PLACEHOLDER") != NULL) {
        ESP_LOGW(TAG, "Embedded credentials are placeholders — falling back to NVS");

        char *nvs_url = nvs_config_get_string(NVS_CONFIG_GATEWAY_CLOUD_URL);
        if (nvs_url && *nvs_url) {
            ws_url = nvs_url;  // intentional leak — used for lifetime of task
        } else {
            if (nvs_url) free(nvs_url);
            ESP_LOGE(TAG, "No WebSocket URL configured. Flash firmware with embedded credentials.");
            while (1) vTaskDelay(pdMS_TO_TICKS(30000));
        }
    }

    // ── Prime self-stats cache before going into the blocking run loop ───────
    {
        PeerMinerState stats;
        build_self_stats(&stats);
        platform_esp32_update_self_stats(&stats);
    }

    // ── Periodic self-stats refresh timer (every 5 seconds) ─────────────────
    esp_timer_handle_t stats_timer;
    esp_timer_create_args_t timer_args = {
        .callback        = self_stats_timer_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "gw_self_stats",
    };
    if (esp_timer_create(&timer_args, &stats_timer) == ESP_OK) {
        esp_timer_start_periodic(stats_timer, 5 * 1000000ULL);  // 5 seconds in µs
    }

    // ── Initialise and run gateway core ──────────────────────────────────────
    const char *version = g_state->SYSTEM_MODULE.version
                            ? g_state->SYSTEM_MODULE.version
                            : "1.0";

    GatewayConfig cfg = {
        .client_id     = client_id,
        .client_secret = client_secret,
        .url           = ws_url,
        .version       = version,
    };

    gateway_core_init(&cfg, &g_state->GATEWAY_MODULE);

    // Blocks indefinitely — reconnects automatically on disconnect.
    gateway_core_run();

    // Never reached under normal operation.
    if (stats_timer) esp_timer_delete(stats_timer);
    vTaskDelete(NULL);
}
