/**
 * ws_client_task.c — thin wrapper around gateway_core for ESP32/BitAxe
 *
 * Responsibilities:
 *   1. Hold the binary-patchable embedded credentials
 *   2. Extract credentials (embedded or NVS fallback) and call gateway_core_init()
 *   3. Call gateway_core_run() — blocks indefinitely, reconnects automatically
 *
 * All protocol logic (tRPC, command dispatch) lives in gateway/src/gateway_core.c.
 * The gateway is now a dumb network proxy — no self-stats push, no peer polling.
 */

#include "ws_client_task.h"
#include "gateway_task.h"
#include "global_state.h"
#include "nvs_config.h"
#include "gateway_core.h"

#include "esp_log.h"
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
    const char *str        = (const char *)raw;
    size_t      prefix_len = strlen(prefix);
    if (strncmp(str, prefix, prefix_len) == 0) {
        return str + prefix_len;
    }
    return str;  // already patched with raw value
}

// ============================================================
// Public accessors (used by HTTP API, screen, etc.)
// ============================================================

static const char *s_resolved_client_id = NULL;

bool        ws_client_is_connected(void)     { return gateway_core_is_connected(); }
bool        ws_client_is_authenticated(void) { return gateway_core_is_authenticated(); }
const char *ws_client_get_client_id(void)    { return s_resolved_client_id; }

// ============================================================
// FreeRTOS task entry point
// ============================================================

void ws_client_task(void *pvParameters)
{
    GlobalState *g_state = (GlobalState *)pvParameters;

    ESP_LOGI(TAG, "WebSocket client task started");

    // ── Resolve credentials ──────────────────────────────────────────────────
    const char *client_id     = get_credential(EMBEDDED_CLIENT_ID,     "HASHLY_CID:");
    const char *client_secret = get_credential(EMBEDDED_CLIENT_SECRET, "HASHLY_SEC:");
    const char *ws_url        = get_credential(EMBEDDED_WS_URL,        "HASHLY_URL:");

    // Fall back to NVS when running with placeholder firmware (dev builds)
    bool url_is_placeholder = strstr(ws_url, "__PLACEHOLDER") != NULL;
    bool cid_is_placeholder = strstr(client_id, "__PLACEHOLDER") != NULL;
    bool sec_is_placeholder = strstr(client_secret, "__PLACEHOLDER") != NULL;

    if (url_is_placeholder || cid_is_placeholder || sec_is_placeholder) {
        ESP_LOGW(TAG, "Embedded credentials are placeholders — falling back to NVS");

        if (url_is_placeholder) {
            char *nvs_url = nvs_config_get_string(NVS_CONFIG_GATEWAY_CLOUD_URL);
            if (nvs_url && *nvs_url) {
                ws_url = nvs_url;  // intentional leak — used for lifetime of task
            } else {
                if (nvs_url) free(nvs_url);
                ESP_LOGE(TAG, "No WebSocket URL configured. Flash firmware with embedded credentials.");
                while (1) vTaskDelay(pdMS_TO_TICKS(30000));
            }
        }

        if (cid_is_placeholder) {
            char *nvs_cid = nvs_config_get_string(NVS_CONFIG_GATEWAY_CLIENT_ID);
            if (nvs_cid && *nvs_cid) {
                client_id = nvs_cid;  // intentional leak — used for lifetime of task
            } else {
                if (nvs_cid) free(nvs_cid);
                ESP_LOGW(TAG, "No client ID configured in NVS");
            }
        }

        if (sec_is_placeholder) {
            char *nvs_sec = nvs_config_get_string(NVS_CONFIG_GATEWAY_CLOUD_API_KEY);
            if (nvs_sec && *nvs_sec) {
                client_secret = nvs_sec;  // intentional leak — used for lifetime of task
            } else {
                if (nvs_sec) free(nvs_sec);
                ESP_LOGW(TAG, "No client secret configured in NVS");
            }
        }
    }

    // Store resolved client_id for ws_client_get_client_id()
    s_resolved_client_id = client_id;

    // ── Initialise and run gateway core ──────────────────────────────────────
    const char *version = "1.1.0";

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
    vTaskDelete(NULL);
}
