/**
 * Hashly Gateway — WebSocket Client Task (Server-Driven Architecture)
 *
 * This is the sole communication task. The ESP32 stores NOTHING except
 * the WebSocket URL and credentials (embedded at build time, binary-patchable).
 *
 * Flow:
 *   1. Connect to Hashly cloud WebSocket
 *   2. Send auth message with embedded clientId/clientSecret
 *   3. Wait for commands from server (poll_miners, scan_network, etc.)
 *   4. Execute commands and send results back
 *
 * The server is the brain — it tells us what to poll, when, and with what IPs.
 * No NVS reads in the main loop. No self-driven polling.
 */

#include "ws_client_task.h"
#include "gateway_task.h"
#include "global_state.h"
#include "nvs_config.h"
#include "miner_adapter.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "cJSON.h"
#include "lwip/sockets.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "ws_client";

// ============================================================
// Embedded credentials — binary-patchable at download time.
//
// These are fixed-size char arrays with known sentinel prefixes.
// The firmware generation pipeline searches for the sentinel bytes
// in the .bin file and overwrites them with real credentials.
//
// Format: "HASHLY_CID:" + value + null padding to fill the field.
// The patcher replaces the entire array contents.
//
// __attribute__((used)) prevents the compiler from optimizing them out.
// ============================================================

#define CREDENTIAL_LEN 128
#define URL_LEN 256

// Embedded credentials — the firmware generation pipeline searches for
// the sentinel bytes in the .bin and overwrites the full array with real values.
//
// IMPORTANT: We use non-const (writable .data section) to prevent GCC from
// merging these into the .rodata string table, which would collapse the arrays
// to just the string length and break binary patching.
// __attribute__((used)) prevents dead-code elimination.
static volatile char __attribute__((used))
    EMBEDDED_CLIENT_ID[CREDENTIAL_LEN]     = "HASHLY_CID:__PLACEHOLDER_CLIENT_ID__";

static volatile char __attribute__((used))
    EMBEDDED_CLIENT_SECRET[CREDENTIAL_LEN] = "HASHLY_SEC:__PLACEHOLDER_CLIENT_SECRET__";

static volatile char __attribute__((used))
    EMBEDDED_WS_URL[URL_LEN]               = "HASHLY_URL:__PLACEHOLDER_WS_URL__";

// Extract the value after the sentinel prefix
static const char *get_credential(volatile const char *raw, const char *prefix)
{
    // Cast away volatile for string operations — the data is const after boot
    const char *str = (const char *)raw;
    size_t prefix_len = strlen(prefix);
    if (strncmp(str, prefix, prefix_len) == 0) {
        return str + prefix_len;
    }
    return str; // No prefix — already patched with raw value
}

// ============================================================
// Module state
// ============================================================

static GlobalState *g_state = NULL;
static esp_websocket_client_handle_t ws_client = NULL;
static volatile bool ws_connected = false;
static volatile bool ws_authenticated = false;
static volatile bool g_scan_stop_requested = false;

// Firmware version (read from global state after init)
#define FW_VERSION "1.0.0"

// ============================================================
// Message queue — decouple event handler from main task
// ============================================================

#define WS_MSG_QUEUE_SIZE 32
#define WS_MSG_MAX_LEN 4096

typedef struct {
    char *data;
    int len;
} ws_msg_t;

static QueueHandle_t ws_msg_queue = NULL;

// ============================================================
// HTTP helper for executing commands against miners
// ============================================================

#define HTTP_RECV_BUF_SIZE 4096
#define CGMINER_RECV_BUF_SIZE 4096

typedef struct {
    char *buf;
    int len;
    int capacity;
} http_response_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_response_t *resp = (http_response_t *)evt->user_data;
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (resp->len + evt->data_len < resp->capacity) {
            memcpy(resp->buf + resp->len, evt->data, evt->data_len);
            resp->len += evt->data_len;
            resp->buf[resp->len] = '\0';
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

// ============================================================
// cgminer TCP command helper (for Canaan miners)
// ============================================================

static bool cgminer_send_command(const char *ip, const char *cmd, char *response, int resp_size)
{
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(4028);
    inet_pton(AF_INET, ip, &dest_addr.sin_addr);

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) return false;

    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0) {
        close(sock);
        return false;
    }

    int sent = send(sock, cmd, strlen(cmd), 0);
    if (sent < 0) {
        close(sock);
        return false;
    }

    int total_read = 0;
    while (total_read < resp_size - 1) {
        int r = recv(sock, response + total_read, resp_size - 1 - total_read, 0);
        if (r <= 0) break;
        total_read += r;
    }
    response[total_read] = '\0';
    close(sock);
    return total_read > 0;
}

// ============================================================
// WebSocket send helper (only call from main task context)
// ============================================================

static void ws_send(const char *data, int len)
{
    if (ws_client && ws_connected) {
        esp_websocket_client_send_text(ws_client, data, len, pdMS_TO_TICKS(5000));
    }
}

static void ws_send_json(cJSON *root)
{
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        ws_send(json_str, strlen(json_str));
        free(json_str);
    }
}

// ============================================================
// Auth — send credentials on connect
// ============================================================

static void send_auth(void)
{
    const char *client_id = get_credential(EMBEDDED_CLIENT_ID, "HASHLY_CID:");
    const char *client_secret = get_credential(EMBEDDED_CLIENT_SECRET, "HASHLY_SEC:");

    // Get gateway's own MAC for identification
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "auth");
    cJSON_AddStringToObject(root, "clientId", client_id);
    cJSON_AddStringToObject(root, "clientSecret", client_secret);
    cJSON_AddStringToObject(root, "firmware", FW_VERSION);

    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(root, "mac", mac_str);

    ws_send_json(root);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Auth sent (clientId=%s, mac=%s)", client_id, mac_str);
}

// ============================================================
// Build status JSON from current peer state
// ============================================================

static char *build_status_json(GatewayModule *gw)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "type", "status");

    cJSON *miners = cJSON_CreateArray();

    for (int i = 0; i < gw->peer_count; i++) {
        PeerMinerState *peer = &gw->peers[i];

        cJSON *m = cJSON_CreateObject();
        cJSON_AddStringToObject(m, "mac", peer->mac_addr);
        cJSON_AddStringToObject(m, "ip", peer->ip);
        cJSON_AddBoolToObject(m, "online", peer->status == PEER_STATUS_ONLINE);

        if (peer->status == PEER_STATUS_ONLINE) {
            cJSON_AddNumberToObject(m, "hashrate", peer->hashrate);
            cJSON_AddNumberToObject(m, "temp", peer->temp);
            cJSON_AddNumberToObject(m, "power", peer->power);
            cJSON_AddNumberToObject(m, "voltage", peer->voltage);
            cJSON_AddNumberToObject(m, "current", peer->current);
            cJSON_AddNumberToObject(m, "sharesAccepted", peer->shares_accepted);
            cJSON_AddNumberToObject(m, "sharesRejected", peer->shares_rejected);
            cJSON_AddNumberToObject(m, "uptimeSeconds", peer->uptime_seconds);
            cJSON_AddNumberToObject(m, "frequency", peer->frequency);

            // Conditional fields — only send when meaningful (avoid 0 for "no sensor")
            if (peer->vr_temp != 0)        cJSON_AddNumberToObject(m, "vrTemp", peer->vr_temp);
            if (peer->fan_pct > 0)         cJSON_AddNumberToObject(m, "fanPct", peer->fan_pct);
            if (peer->fan_rpm > 0)         cJSON_AddNumberToObject(m, "fanRpm", peer->fan_rpm);
            if (peer->wifi_rssi != 0)      cJSON_AddNumberToObject(m, "wifiRssi", peer->wifi_rssi);
            if (peer->found_blocks > 0)    cJSON_AddNumberToObject(m, "foundBlocks", peer->found_blocks);
            if (peer->best_diff > 0)       cJSON_AddNumberToObject(m, "bestDiff", peer->best_diff);
            if (peer->best_session_diff > 0) cJSON_AddNumberToObject(m, "bestSessionDiff", peer->best_session_diff);
            if (peer->pool_difficulty > 0) cJSON_AddNumberToObject(m, "poolDiff", peer->pool_difficulty);

            if (peer->asic_model[0] != '\0')
                cJSON_AddStringToObject(m, "model", peer->asic_model);
            if (peer->device_model[0] != '\0')
                cJSON_AddStringToObject(m, "deviceModel", peer->device_model);

            if (peer->temp_avg > 0) cJSON_AddNumberToObject(m, "tempAvg", peer->temp_avg);
            if (peer->temp_max > 0) cJSON_AddNumberToObject(m, "tempMax", peer->temp_max);

            // Pools array
            if (peer->pool_count > 0) {
                cJSON *pools = cJSON_CreateArray();
                for (int pi = 0; pi < peer->pool_count; pi++) {
                    PoolInfo *p = &peer->pools[pi];
                    cJSON *pj = cJSON_CreateObject();
                    cJSON_AddStringToObject(pj, "url", p->url);
                    cJSON_AddStringToObject(pj, "user", p->user);
                    cJSON_AddBoolToObject(pj, "active", p->active);
                    if (p->stratum_diff > 0)
                        cJSON_AddNumberToObject(pj, "diff", p->stratum_diff);
                    if (p->accepted > 0)
                        cJSON_AddNumberToObject(pj, "accepted", p->accepted);
                    if (p->rejected > 0)
                        cJSON_AddNumberToObject(pj, "rejected", p->rejected);
                    cJSON_AddItemToArray(pools, pj);
                }
                cJSON_AddItemToObject(m, "pools", pools);
            }
        } else if (peer->status == PEER_STATUS_ERROR) {
            cJSON_AddStringToObject(m, "error", "Poll failed");
        }

        cJSON_AddItemToArray(miners, m);
    }

    cJSON_AddItemToObject(root, "miners", miners);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

// ============================================================
// Command handlers
// ============================================================

/**
 * Handle poll_miners command from server.
 * Server sends list of miners with IPs — we poll each one and send back status.
 *
 * Input:  { type: "command", action: "poll_miners", miners: [{macAddress, ip, adapter},...] }
 * Output: { type: "status", miners: [{mac, ip, online, hashrate, ...},...] }
 */
static void handle_poll_miners(cJSON *root)
{
    GatewayModule *gw = &g_state->GATEWAY_MODULE;

    // Skip polling while a network scan is running — the scan blocks HTTP
    // and poll commands just fill the message queue.
    if (gw->SCAN_STATE.scanning) {
        ESP_LOGD(TAG, "Skipping poll_miners during scan");
        return;
    }

    cJSON *miners_arr = cJSON_GetObjectItem(root, "miners");
    if (!cJSON_IsArray(miners_arr)) return;

    int count = cJSON_GetArraySize(miners_arr);
    // Reserve one slot for self-status (gateway is both connector + miner)
    if (count > GATEWAY_MAX_PEERS - 1) count = GATEWAY_MAX_PEERS - 1;

    gw->peer_count = 0;
    gw->is_polling = true;

    for (int i = 0; i < count; i++) {
        cJSON *entry = cJSON_GetArrayItem(miners_arr, i);
        if (!cJSON_IsObject(entry)) continue;

        cJSON *ip_item = cJSON_GetObjectItem(entry, "ip");
        cJSON *mac_item = cJSON_GetObjectItem(entry, "macAddress");
        cJSON *adapter_item = cJSON_GetObjectItem(entry, "adapter");

        if (!cJSON_IsString(ip_item) || strlen(ip_item->valuestring) == 0) continue;

        PeerMinerState *peer = &gw->peers[gw->peer_count];
        memset(peer, 0, sizeof(PeerMinerState));

        strncpy(peer->ip, ip_item->valuestring, GATEWAY_IP_MAX_LEN - 1);
        if (cJSON_IsString(mac_item)) {
            strncpy(peer->mac_addr, mac_item->valuestring, sizeof(peer->mac_addr) - 1);
        }

        MinerType type = MINER_TYPE_UNKNOWN;
        if (cJSON_IsString(adapter_item)) {
            type = miner_type_from_string(adapter_item->valuestring);
        }
        peer->type = type;

        // Poll the miner
        bool ok = poll_miner(peer->ip, peer->type, peer);
        if (ok) {
            peer->status = PEER_STATUS_ONLINE;
            peer->consecutive_failures = 0;
            peer->last_seen = esp_timer_get_time();
        } else {
            peer->consecutive_failures++;
            peer->status = (peer->consecutive_failures >= 3)
                ? PEER_STATUS_OFFLINE : PEER_STATUS_ERROR;
        }

        gw->peer_count++;
    }

    // Self-status: read directly from g_state (can't HTTP-poll ourselves)
    if (gw->peer_count < GATEWAY_MAX_PEERS && g_state->ASIC_initalized) {
        PeerMinerState *self = &gw->peers[gw->peer_count];
        memset(self, 0, sizeof(PeerMinerState));

        // IP + MAC
        esp_netif_ip_info_t self_ip_info;
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif && esp_netif_get_ip_info(netif, &self_ip_info) == ESP_OK && self_ip_info.ip.addr != 0) {
            snprintf(self->ip, GATEWAY_IP_MAX_LEN, IPSTR, IP2STR(&self_ip_info.ip));
        }
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(self->mac_addr, sizeof(self->mac_addr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        self->type = MINER_TYPE_BITAXE;
        self->status = PEER_STATUS_ONLINE;
        self->last_seen = esp_timer_get_time();

        SystemModule *sys = &g_state->SYSTEM_MODULE;
        PowerManagementModule *pwr = &g_state->POWER_MANAGEMENT_MODULE;
        DeviceConfig *dev = &g_state->DEVICE_CONFIG;

        // Mining stats
        self->hashrate = sys->current_hashrate;
        self->temp = pwr->chip_temp_avg;
        self->temp_avg = pwr->chip_temp_avg;
        self->temp_max = pwr->chip_temp_avg;
        self->vr_temp = pwr->vr_temp;
        self->power = pwr->power;
        self->voltage = pwr->voltage;
        self->current = pwr->current;
        self->frequency = pwr->frequency_value;
        self->fan_pct = (int)pwr->fan_perc;
        self->fan_rpm = (int)pwr->fan_rpm;
        self->shares_accepted = sys->shares_accepted;
        self->shares_rejected = sys->shares_rejected;
        self->best_diff = (double)sys->best_nonce_diff;
        self->best_session_diff = (double)sys->best_session_nonce_diff;
        self->found_blocks = sys->block_found;
        self->pool_difficulty = (double)sys->pool_difficulty;
        self->uptime_seconds = (uint32_t)((esp_timer_get_time() - sys->start_time) / 1000000);

        // Device info
        if (dev->family.asic.name) {
            strncpy(self->asic_model, dev->family.asic.name, sizeof(self->asic_model) - 1);
        }
        if (dev->family.name) {
            strncpy(self->device_model, dev->family.name, sizeof(self->device_model) - 1);
        }

        // Hostname from NVS
        char *hostname = nvs_config_get_string(NVS_CONFIG_HOSTNAME);
        if (hostname) {
            strncpy(self->hostname, hostname, sizeof(self->hostname) - 1);
            free(hostname);
        }

        // Pool info
        self->pool_count = 0;
        if (sys->pool_url && sys->pool_user) {
            PoolInfo *p = &self->pools[self->pool_count];
            memset(p, 0, sizeof(PoolInfo));
            snprintf(p->url, sizeof(p->url), "%s:%u", sys->pool_url, sys->pool_port);
            strncpy(p->user, sys->pool_user, sizeof(p->user) - 1);
            p->active = !sys->is_using_fallback;
            p->stratum_diff = sys->pool_difficulty;
            p->accepted = sys->shares_accepted;
            p->rejected = sys->shares_rejected;
            self->pool_count++;
        }
        if (sys->fallback_pool_url && sys->fallback_pool_user) {
            PoolInfo *p = &self->pools[self->pool_count];
            memset(p, 0, sizeof(PoolInfo));
            snprintf(p->url, sizeof(p->url), "%s:%u", sys->fallback_pool_url, sys->fallback_pool_port);
            strncpy(p->user, sys->fallback_pool_user, sizeof(p->user) - 1);
            p->active = sys->is_using_fallback;
            p->stratum_diff = sys->fallback_pool_difficulty;
            self->pool_count++;
        }

        gw->peer_count++;
    }

    gw->is_polling = false;

    // Send status back
    char *status_json = build_status_json(gw);
    if (status_json) {
        ws_send(status_json, strlen(status_json));
        free(status_json);
    }
}

// Context passed to scan callback for streaming results
typedef struct {
    const char *command_id;
    int pending_count;
    DiscoveredMiner pending[3];
} scan_stream_ctx_t;

#define SCAN_BATCH_SIZE 3

// Flush pending scan results over WebSocket
static void flush_scan_batch(scan_stream_ctx_t *sctx)
{
    if (sctx->pending_count == 0) return;

    for (int i = 0; i < sctx->pending_count; i++) {
        DiscoveredMiner *miner = &sctx->pending[i];

        cJSON *r = cJSON_CreateObject();
        cJSON_AddStringToObject(r, "ip", miner->ip);
        cJSON_AddStringToObject(r, "mac", miner->mac);
        cJSON_AddStringToObject(r, "type", miner_type_to_string(miner->type));
        cJSON_AddStringToObject(r, "model", miner->model);
        cJSON_AddStringToObject(r, "hostname", miner->hostname);
        cJSON_AddNumberToObject(r, "hashrate", miner->hashrate);
        cJSON_AddBoolToObject(r, "reachable", miner->reachable);

        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "type", "command_result");
        if (sctx->command_id) {
            cJSON_AddStringToObject(resp, "commandId", sctx->command_id);
        }
        cJSON_AddBoolToObject(resp, "success", true);
        cJSON_AddBoolToObject(resp, "partial", true);
        cJSON_AddItemToObject(resp, "result", r);

        ws_send_json(resp);
        cJSON_Delete(resp);
    }

    sctx->pending_count = 0;
}

// Callback: buffer discovered miners and send in batches
static void on_scan_miner_found(const DiscoveredMiner *miner, void *ctx)
{
    scan_stream_ctx_t *sctx = (scan_stream_ctx_t *)ctx;

    sctx->pending[sctx->pending_count] = *miner;
    sctx->pending_count++;

    if (sctx->pending_count >= SCAN_BATCH_SIZE) {
        flush_scan_batch(sctx);
    }
}

/**
 * Handle scan_network command from server.
 *
 * Input:  { type: "command", action: "scan_network", commandId: "...", cidr: "192.168.1.0/24" }
 * Output: Streams partial command_result for each miner found, then a final command_result.
 */
static void handle_scan_network(cJSON *root)
{
    GatewayModule *gw = &g_state->GATEWAY_MODULE;

    cJSON *cmd_id_item = cJSON_GetObjectItem(root, "commandId");
    const char *command_id = cJSON_IsString(cmd_id_item) ? cmd_id_item->valuestring : NULL;

    // Ignore duplicate scan commands while scanning
    if (gw->SCAN_STATE.scanning) {
        ESP_LOGW(TAG, "Scan already in progress, ignoring duplicate command");
        return;
    }

    // Send ACK
    if (command_id) {
        cJSON *ack = cJSON_CreateObject();
        cJSON_AddStringToObject(ack, "type", "command_ack");
        cJSON_AddStringToObject(ack, "commandId", command_id);
        ws_send_json(ack);
        cJSON_Delete(ack);
    }

    ESP_LOGI(TAG, "Starting network scan...");

    // Run scan — streams each found miner via callback
    memset(&gw->SCAN_STATE, 0, sizeof(ScanState));
    gw->SCAN_STATE.scanning = true;
    g_scan_stop_requested = false;

    scan_stream_ctx_t stream_ctx = { .command_id = command_id, .pending_count = 0 };
    run_network_scan(&gw->SCAN_STATE, on_scan_miner_found, &stream_ctx);

    // Flush any remaining buffered results
    flush_scan_batch(&stream_ctx);

    ESP_LOGI(TAG, "Scan complete: %d miners found", gw->SCAN_STATE.result_count);

    // Send final command_result (no partial flag = scan complete)
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "type", "command_result");
    if (command_id) {
        cJSON_AddStringToObject(resp, "commandId", command_id);
    }
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddNumberToObject(resp, "totalFound", gw->SCAN_STATE.result_count);

    // Diagnostic error counts
    cJSON *diag = cJSON_CreateObject();
    cJSON_AddNumberToObject(diag, "errAlloc", gw->SCAN_STATE.err_alloc);
    cJSON_AddNumberToObject(diag, "errInit", gw->SCAN_STATE.err_init);
    cJSON_AddNumberToObject(diag, "errPerform", gw->SCAN_STATE.err_perform);
    cJSON_AddNumberToObject(diag, "errStatus", gw->SCAN_STATE.err_status);
    cJSON_AddNumberToObject(diag, "errParse", gw->SCAN_STATE.err_parse);
    cJSON_AddNumberToObject(diag, "freeHeap", (double)esp_get_free_heap_size());
    cJSON_AddItemToObject(resp, "diagnostics", diag);

    ws_send_json(resp);
    cJSON_Delete(resp);
}

/**
 * Handle stop_scan command from server.
 * Sets the abort flag so the scan loop exits early.
 * The fast-path in ws_event_handler already set the flag; this handler
 * runs after the scan returns and sends an ACK.
 */
static void handle_stop_scan(cJSON *root)
{
    GatewayModule *gw = &g_state->GATEWAY_MODULE;
    cJSON *cmd_id_item = cJSON_GetObjectItem(root, "commandId");
    const char *command_id = cJSON_IsString(cmd_id_item) ? cmd_id_item->valuestring : NULL;

    g_scan_stop_requested = true;
    gw->SCAN_STATE.stop_requested = true;
    gw->SCAN_STATE.scanning = false;

    if (command_id) {
        cJSON *ack = cJSON_CreateObject();
        cJSON_AddStringToObject(ack, "type", "command_ack");
        cJSON_AddStringToObject(ack, "commandId", command_id);
        ws_send_json(ack);
        cJSON_Delete(ack);
    }

    ESP_LOGI(TAG, "Scan stop handled");
}

/**
 * Handle miner-targeted commands: restart_miner, get_config, set_config, identify, patch_settings
 */
static void handle_miner_command(cJSON *root, const char *action)
{
    cJSON *cmd_id_item = cJSON_GetObjectItem(root, "commandId");
    const char *command_id = cJSON_IsString(cmd_id_item) ? cmd_id_item->valuestring : NULL;

    // Find target IP — server sends macAddress, we look up from our peer cache
    cJSON *mac_item = cJSON_GetObjectItem(root, "macAddress");
    cJSON *ip_item = cJSON_GetObjectItem(root, "ip");
    const char *target_mac = cJSON_IsString(mac_item) ? mac_item->valuestring : "";
    const char *target_ip = cJSON_IsString(ip_item) ? ip_item->valuestring : NULL;

    // If no IP provided, try to find from current peer state
    if (!target_ip || strlen(target_ip) == 0) {
        GatewayModule *gw = &g_state->GATEWAY_MODULE;
        for (int i = 0; i < gw->peer_count; i++) {
            if (strcasecmp(gw->peers[i].mac_addr, target_mac) == 0) {
                target_ip = gw->peers[i].ip;
                break;
            }
        }
    }

    if (!target_ip || strlen(target_ip) == 0) {
        // Can't find target — send failure
        if (command_id) {
            cJSON *resp = cJSON_CreateObject();
            cJSON_AddStringToObject(resp, "type", "command_result");
            cJSON_AddStringToObject(resp, "commandId", command_id);
            cJSON_AddBoolToObject(resp, "success", false);
            cJSON_AddStringToObject(resp, "error", "No IP for target miner");
            ws_send_json(resp);
            cJSON_Delete(resp);
        }
        return;
    }

    // Send ACK
    if (command_id) {
        cJSON *ack = cJSON_CreateObject();
        cJSON_AddStringToObject(ack, "type", "command_ack");
        cJSON_AddStringToObject(ack, "commandId", command_id);
        ws_send_json(ack);
        cJSON_Delete(ack);
    }

    // Map gateway actions to miner HTTP actions
    const char *http_action = action;
    if (strcmp(action, "restart_miner") == 0) http_action = "restart";

    // Check if this is a Canaan miner (cgminer protocol)
    GatewayModule *gw = &g_state->GATEWAY_MODULE;
    MinerType target_type = MINER_TYPE_UNKNOWN;
    for (int i = 0; i < gw->peer_count; i++) {
        if (strcasecmp(gw->peers[i].mac_addr, target_mac) == 0 ||
            strcmp(gw->peers[i].ip, target_ip) == 0) {
            target_type = gw->peers[i].type;
            break;
        }
    }

    // Also check the adapter field from the command (peer cache may be stale)
    if (target_type == MINER_TYPE_UNKNOWN) {
        cJSON *adapter_item = cJSON_GetObjectItem(root, "adapter");
        if (cJSON_IsString(adapter_item)) {
            if (strcasecmp(adapter_item->valuestring, "AvalonNano3s") == 0 ||
                strcasecmp(adapter_item->valuestring, "AvalonQ") == 0 ||
                strcasecmp(adapter_item->valuestring, "AvalonMini3") == 0) {
                target_type = MINER_TYPE_CANAAN;
            }
        }
    }

    // ── Canaan/Avalon: all commands via cgminer TCP port 4028 ──
    if (target_type == MINER_TYPE_CANAAN) {
        char *resp_buf = heap_caps_malloc(CGMINER_RECV_BUF_SIZE, MALLOC_CAP_SPIRAM);
        if (!resp_buf) {
            if (command_id) {
                cJSON *result = cJSON_CreateObject();
                cJSON_AddStringToObject(result, "type", "command_result");
                cJSON_AddStringToObject(result, "commandId", command_id);
                cJSON_AddBoolToObject(result, "success", false);
                cJSON_AddStringToObject(result, "error", "Memory allocation failed");
                ws_send_json(result);
                cJSON_Delete(result);
            }
            return;
        }

        if (strcmp(http_action, "restart") == 0) {
            // Restart via ascset reboot (connection may drop — that's expected)
            bool ok = cgminer_send_command(target_ip,
                "{\"command\":\"ascset\",\"parameter\":\"0,reboot,0\"}",
                resp_buf, CGMINER_RECV_BUF_SIZE);
            // Treat send success as ok even if connection drops (device is rebooting)
            cJSON *result = cJSON_CreateObject();
            cJSON_AddStringToObject(result, "type", "command_result");
            if (command_id) cJSON_AddStringToObject(result, "commandId", command_id);
            cJSON_AddBoolToObject(result, "success", true);
            ws_send_json(result);
            cJSON_Delete(result);
            (void)ok; // suppress unused warning

        } else if (strcmp(http_action, "identify") == 0) {
            // Flash LED on device
            cgminer_send_command(target_ip,
                "{\"command\":\"ascidentify\",\"parameter\":\"0\"}",
                resp_buf, CGMINER_RECV_BUF_SIZE);
            cJSON *result = cJSON_CreateObject();
            cJSON_AddStringToObject(result, "type", "command_result");
            if (command_id) cJSON_AddStringToObject(result, "commandId", command_id);
            cJSON_AddBoolToObject(result, "success", true);
            ws_send_json(result);
            cJSON_Delete(result);

        } else if (strcmp(http_action, "get_config") == 0) {
            // Build config from version + pools + estats
            cJSON *config_result = cJSON_CreateObject();
            cJSON_AddBoolToObject(config_result, "cgminer", true);

            // 1. Version (model)
            if (cgminer_send_command(target_ip, "{\"command\":\"version\"}", resp_buf, CGMINER_RECV_BUF_SIZE)) {
                cJSON *root_v = cJSON_Parse(resp_buf);
                if (root_v) {
                    cJSON *ver_arr = cJSON_GetObjectItem(root_v, "VERSION");
                    if (cJSON_IsArray(ver_arr) && cJSON_GetArraySize(ver_arr) > 0) {
                        cJSON *ver = cJSON_GetArrayItem(ver_arr, 0);
                        cJSON *prod = cJSON_GetObjectItem(ver, "PROD");
                        if (cJSON_IsString(prod)) {
                            cJSON_AddStringToObject(config_result, "model", prod->valuestring);
                        }
                    }
                    cJSON_Delete(root_v);
                }
            }

            // 2. Pools
            if (cgminer_send_command(target_ip, "{\"command\":\"pools\"}", resp_buf, CGMINER_RECV_BUF_SIZE)) {
                cJSON *root_p = cJSON_Parse(resp_buf);
                if (root_p) {
                    cJSON *pools_arr = cJSON_GetObjectItem(root_p, "POOLS");
                    if (cJSON_IsArray(pools_arr)) {
                        cJSON *pools_out = cJSON_CreateArray();
                        int pcount = cJSON_GetArraySize(pools_arr);
                        for (int p = 0; p < pcount; p++) {
                            cJSON *pool = cJSON_GetArrayItem(pools_arr, p);
                            cJSON *pool_obj = cJSON_CreateObject();
                            cJSON *url = cJSON_GetObjectItem(pool, "URL");
                            cJSON *user = cJSON_GetObjectItem(pool, "User");
                            if (cJSON_IsString(url)) cJSON_AddStringToObject(pool_obj, "url", url->valuestring);
                            if (cJSON_IsString(user)) cJSON_AddStringToObject(pool_obj, "username", user->valuestring);
                            cJSON_AddItemToArray(pools_out, pool_obj);
                        }
                        cJSON_AddItemToObject(config_result, "pools", pools_out);
                    }
                    cJSON_Delete(root_p);
                }
            }

            // 3. Estats (frequency, workMode, power, targetTemp from MM ID0)
            if (cgminer_send_command(target_ip, "{\"command\":\"estats\"}", resp_buf, CGMINER_RECV_BUF_SIZE)) {
                cJSON *root_e = cJSON_Parse(resp_buf);
                if (root_e) {
                    cJSON *stats_arr = cJSON_GetObjectItem(root_e, "STATS");
                    if (cJSON_IsArray(stats_arr) && cJSON_GetArraySize(stats_arr) > 0) {
                        cJSON *stats = cJSON_GetArrayItem(stats_arr, 0);

                        // Try "MM ID0" then "MM ID0:Summary"
                        cJSON *mmid0_item = cJSON_GetObjectItem(stats, "MM ID0");
                        if (!cJSON_IsString(mmid0_item)) {
                            mmid0_item = cJSON_GetObjectItem(stats, "MM ID0:Summary");
                        }

                        if (cJSON_IsString(mmid0_item)) {
                            const char *mmid0 = mmid0_item->valuestring;
                            char val[64];

                            if (mmget(mmid0, "Freq", val, sizeof(val))) {
                                cJSON_AddNumberToObject(config_result, "frequency", atof(val));
                            }
                            if (mmget(mmid0, "WORKMODE", val, sizeof(val))) {
                                cJSON_AddNumberToObject(config_result, "workMode", atoi(val));
                            }
                            if (mmget(mmid0, "WORKLEVEL", val, sizeof(val))) {
                                cJSON_AddNumberToObject(config_result, "workLevel", atoi(val));
                            }
                            if (mmget(mmid0, "MPO", val, sizeof(val))) {
                                cJSON_AddNumberToObject(config_result, "powerLimit", atof(val));
                            }
                            if (mmget(mmid0, "TarT", val, sizeof(val))) {
                                cJSON_AddNumberToObject(config_result, "targetTemp", atof(val));
                            }
                            if (mmget(mmid0, "FanR", val, sizeof(val))) {
                                cJSON_AddNumberToObject(config_result, "fanSpeed", atoi(val));
                            }
                        }
                    }
                    cJSON_Delete(root_e);
                }
            }

            // Send result
            cJSON *result = cJSON_CreateObject();
            cJSON_AddStringToObject(result, "type", "command_result");
            if (command_id) cJSON_AddStringToObject(result, "commandId", command_id);
            cJSON_AddBoolToObject(result, "success", true);
            cJSON_AddItemToObject(result, "result", config_result);
            ws_send_json(result);
            cJSON_Delete(result);

        } else if (strcmp(http_action, "set_config") == 0) {
            // Parse config fields from command and send cgminer commands
            cJSON *config_obj = cJSON_GetObjectItem(root, "config");
            bool ok = true;
            bool needs_reboot = false;

            if (cJSON_IsObject(config_obj)) {
                // Work mode: ascset|0,workmode,set,N
                cJSON *wm = cJSON_GetObjectItem(config_obj, "workMode");
                if (cJSON_IsNumber(wm)) {
                    char cmd[128];
                    snprintf(cmd, sizeof(cmd),
                             "{\"command\":\"ascset\",\"parameter\":\"0,workmode,set,%d\"}",
                             wm->valueint);
                    cgminer_send_command(target_ip, cmd, resp_buf, CGMINER_RECV_BUF_SIZE);
                    needs_reboot = true;
                }

                // Work level: ascset|0,worklevel,set,N (Mini3)
                cJSON *wl = cJSON_GetObjectItem(config_obj, "workLevel");
                if (cJSON_IsNumber(wl)) {
                    char cmd[128];
                    snprintf(cmd, sizeof(cmd),
                             "{\"command\":\"ascset\",\"parameter\":\"0,worklevel,set,%d\"}",
                             wl->valueint);
                    cgminer_send_command(target_ip, cmd, resp_buf, CGMINER_RECV_BUF_SIZE);
                    needs_reboot = true;
                }

                // Fan speed: ascset|0,fan-spd,N (AvalonQ)
                cJSON *fs = cJSON_GetObjectItem(config_obj, "fanSpeed");
                if (cJSON_IsNumber(fs)) {
                    char cmd[128];
                    snprintf(cmd, sizeof(cmd),
                             "{\"command\":\"ascset\",\"parameter\":\"0,fan-spd,%d\"}",
                             fs->valueint);
                    cgminer_send_command(target_ip, cmd, resp_buf, CGMINER_RECV_BUF_SIZE);
                }

                // Pools: setpool|admin,admin,<index>,<url>,<worker>,<password>
                cJSON *pools = cJSON_GetObjectItem(config_obj, "pools");
                if (cJSON_IsArray(pools)) {
                    int pcount = cJSON_GetArraySize(pools);
                    for (int p = 0; p < pcount; p++) {
                        cJSON *pool = cJSON_GetArrayItem(pools, p);
                        cJSON *url = cJSON_GetObjectItem(pool, "url");
                        cJSON *user = cJSON_GetObjectItem(pool, "username");
                        cJSON *pass = cJSON_GetObjectItem(pool, "password");

                        if (cJSON_IsString(url) && cJSON_IsString(user)) {
                            // Build setpool command as pipe-delimited (simpler for cgminer)
                            char cmd[512];
                            snprintf(cmd, sizeof(cmd),
                                     "{\"command\":\"setpool\",\"parameter\":\"admin,admin,%d,%s,%s,%s\"}",
                                     p,
                                     url->valuestring,
                                     user->valuestring,
                                     cJSON_IsString(pass) ? pass->valuestring : "");
                            cgminer_send_command(target_ip, cmd, resp_buf, CGMINER_RECV_BUF_SIZE);
                        }
                    }
                    needs_reboot = true;
                }

                // Reboot if needed (pool/workmode changes require restart)
                if (needs_reboot) {
                    vTaskDelay(pdMS_TO_TICKS(500)); // Brief delay before reboot
                    cgminer_send_command(target_ip,
                        "{\"command\":\"ascset\",\"parameter\":\"0,reboot,0\"}",
                        resp_buf, CGMINER_RECV_BUF_SIZE);
                }
            }

            cJSON *result = cJSON_CreateObject();
            cJSON_AddStringToObject(result, "type", "command_result");
            if (command_id) cJSON_AddStringToObject(result, "commandId", command_id);
            cJSON_AddBoolToObject(result, "success", ok);
            ws_send_json(result);
            cJSON_Delete(result);

        } else {
            // Unsupported action for Canaan
            ESP_LOGW(TAG, "Unsupported Canaan action: %s", http_action);
            cJSON *result = cJSON_CreateObject();
            cJSON_AddStringToObject(result, "type", "command_result");
            if (command_id) cJSON_AddStringToObject(result, "commandId", command_id);
            cJSON_AddBoolToObject(result, "success", false);
            cJSON_AddStringToObject(result, "error", "Unsupported action for Canaan miner");
            ws_send_json(result);
            cJSON_Delete(result);
        }

        free(resp_buf);
        ESP_LOGI(TAG, "Canaan command %s -> %s: done", http_action, target_ip);
        return;
    }

    // AxeOS HTTP command
    char url[128];
    esp_http_client_method_t method = HTTP_METHOD_POST;
    cJSON *data_item = cJSON_GetObjectItem(root, "config");
    char *post_data = NULL;

    if (strcmp(http_action, "patch_settings") == 0 || strcmp(http_action, "set_config") == 0) {
        snprintf(url, sizeof(url), "http://%s/api/system", target_ip);
        method = HTTP_METHOD_PATCH;
        if (data_item) {
            post_data = cJSON_PrintUnformatted(data_item);
        }
    } else if (strcmp(http_action, "restart") == 0) {
        snprintf(url, sizeof(url), "http://%s/api/system/restart", target_ip);
    } else if (strcmp(http_action, "identify") == 0) {
        snprintf(url, sizeof(url), "http://%s/api/system/identify", target_ip);
    } else if (strcmp(http_action, "get_config") == 0) {
        snprintf(url, sizeof(url), "http://%s/api/system/info", target_ip);
        method = HTTP_METHOD_GET;
    } else {
        ESP_LOGW(TAG, "Unknown miner action: %s", http_action);
        if (command_id) {
            cJSON *resp = cJSON_CreateObject();
            cJSON_AddStringToObject(resp, "type", "command_result");
            cJSON_AddStringToObject(resp, "commandId", command_id);
            cJSON_AddBoolToObject(resp, "success", false);
            cJSON_AddStringToObject(resp, "error", "Unknown action");
            ws_send_json(resp);
            cJSON_Delete(resp);
        }
        return;
    }

    char *recv_buf = heap_caps_malloc(HTTP_RECV_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!recv_buf) {
        if (post_data) free(post_data);
        return;
    }

    http_response_t resp = { .buf = recv_buf, .len = 0, .capacity = HTTP_RECV_BUF_SIZE };

    esp_http_client_config_t config = {
        .url = url,
        .method = method,
        .timeout_ms = 10000,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .disable_auto_redirect = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(recv_buf);
        if (post_data) free(post_data);
        return;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (post_data) {
        esp_http_client_set_post_field(client, post_data, strlen(post_data));
    }

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (post_data) free(post_data);

    bool success = (err == ESP_OK && status_code >= 200 && status_code < 300);

    // Build command_result
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "type", "command_result");
    if (command_id) cJSON_AddStringToObject(result, "commandId", command_id);
    cJSON_AddBoolToObject(result, "success", success);

    if (!success) {
        char err_msg[64];
        snprintf(err_msg, sizeof(err_msg), "HTTP %d (err=%d)", status_code, err);
        cJSON_AddStringToObject(result, "error", err_msg);
    } else if (resp.len > 0) {
        // Try to parse response as JSON and include in result
        cJSON *resp_json = cJSON_Parse(recv_buf);
        if (resp_json) {
            cJSON_AddItemToObject(result, "result", resp_json);
        }
    }

    free(recv_buf);

    ws_send_json(result);
    cJSON_Delete(result);

    ESP_LOGI(TAG, "Command %s -> %s: %s (status=%d)", http_action, target_ip,
             success ? "OK" : "FAIL", status_code);
}

// ============================================================
// Message dispatcher
// ============================================================

static void process_message(const char *msg_data, int msg_len)
{
    cJSON *root = cJSON_ParseWithLength(msg_data, msg_len);
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse message");
        return;
    }

    cJSON *type_item = cJSON_GetObjectItem(root, "type");
    const char *type = cJSON_IsString(type_item) ? type_item->valuestring : "?";

    // Handle auth response
    if (strcmp(type, "auth_ok") == 0) {
        ws_authenticated = true;
        cJSON *cid = cJSON_GetObjectItem(root, "connectorId");
        ESP_LOGI(TAG, "Authenticated! connectorId=%s",
                 cJSON_IsString(cid) ? cid->valuestring : "?");
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type, "auth_error") == 0) {
        cJSON *msg = cJSON_GetObjectItem(root, "message");
        ESP_LOGE(TAG, "Auth failed: %s",
                 cJSON_IsString(msg) ? msg->valuestring : "unknown");
        cJSON_Delete(root);
        return;
    }

    // All other messages require authentication
    if (!ws_authenticated) {
        ESP_LOGW(TAG, "Ignoring message before auth: type=%s", type);
        cJSON_Delete(root);
        return;
    }

    // Handle commands from server
    if (strcmp(type, "command") == 0) {
        cJSON *action_item = cJSON_GetObjectItem(root, "action");
        if (!cJSON_IsString(action_item)) {
            ESP_LOGW(TAG, "Command missing 'action'");
            cJSON_Delete(root);
            return;
        }

        const char *action = action_item->valuestring;
        ESP_LOGI(TAG, "Command: %s", action);

        if (strcmp(action, "poll_miners") == 0) {
            handle_poll_miners(root);
        } else if (strcmp(action, "scan_network") == 0) {
            handle_scan_network(root);
        } else if (strcmp(action, "stop_scan") == 0) {
            handle_stop_scan(root);
        } else {
            // Miner-targeted commands
            handle_miner_command(root, action);
        }
    } else {
        ESP_LOGD(TAG, "Ignoring message type: %s", type);
    }

    cJSON_Delete(root);
}

// ============================================================
// WebSocket event handler — runs in transport task context.
// MUST NOT call esp_websocket_client_send_text() here.
// ============================================================

static void ws_event_handler(void *handler_args, esp_event_base_t base,
                             int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket connected");
        ws_connected = true;
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WebSocket disconnected");
        ws_connected = false;
        ws_authenticated = false;
        break;

    case WEBSOCKET_EVENT_DATA:
        if (data->op_code == 0x01) { // Text frame
            if (data->data_len > 0 && data->data_ptr && ws_msg_queue) {
                int copy_len = data->data_len;
                if (copy_len > WS_MSG_MAX_LEN) copy_len = WS_MSG_MAX_LEN;

                char *copy = heap_caps_malloc(copy_len + 1, MALLOC_CAP_SPIRAM);
                if (copy) {
                    memcpy(copy, data->data_ptr, copy_len);
                    copy[copy_len] = '\0';

                    // Fast-path: detect stop_scan in transport task context
                    // so the scan loop sees the flag immediately (main task is blocked)
                    if (strstr(copy, "\"stop_scan\"") != NULL) {
                        g_scan_stop_requested = true;
                        if (g_state) {
                            g_state->GATEWAY_MODULE.SCAN_STATE.stop_requested = true;
                        }
                        ESP_LOGI(TAG, "Stop scan requested (fast-path)");
                    }

                    ws_msg_t msg = { .data = copy, .len = copy_len };
                    if (xQueueSend(ws_msg_queue, &msg, 0) != pdTRUE) {
                        ESP_LOGW(TAG, "Message queue full, dropping");
                        free(copy);
                    }
                }
            }
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WebSocket error");
        ws_connected = false;
        ws_authenticated = false;
        break;

    default:
        break;
    }
}

// Process all queued messages (called from main task loop)
static void process_queued_messages(void)
{
    ws_msg_t msg;
    while (xQueueReceive(ws_msg_queue, &msg, 0) == pdTRUE) {
        process_message(msg.data, msg.len);
        free(msg.data);
    }
}

// ============================================================
// Main task
// ============================================================

void ws_client_task(void *pvParameters)
{
    g_state = (GlobalState *)pvParameters;

    // Create message queue
    ws_msg_queue = xQueueCreate(WS_MSG_QUEUE_SIZE, sizeof(ws_msg_t));
    if (!ws_msg_queue) {
        ESP_LOGE(TAG, "Failed to create message queue");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "WebSocket client task started (server-driven mode)");

    // Get WebSocket URL from embedded credentials
    const char *ws_url = get_credential(EMBEDDED_WS_URL, "HASHLY_URL:");

    // Check if credentials are still placeholders
    if (strstr(ws_url, "__PLACEHOLDER") != NULL) {
        // Fall back to NVS for development/testing
        ESP_LOGW(TAG, "Embedded credentials are placeholders, falling back to NVS");
        char *nvs_url = nvs_config_get_string(NVS_CONFIG_GATEWAY_CLOUD_URL);
        if (nvs_url && strlen(nvs_url) > 0) {
            ws_url = nvs_url;
            // Note: leaks nvs_url intentionally — it's used for the lifetime of this task
        } else {
            if (nvs_url) free(nvs_url);
            ESP_LOGE(TAG, "No WebSocket URL configured. Flash firmware with embedded credentials.");
            // Wait indefinitely but allow task to be deleted
            while (1) {
                vTaskDelay(30000 / portTICK_PERIOD_MS);
            }
        }
    }

    while (1) {
        // Safety net: if internal heap is critically low, reboot cleanly
        size_t free_internal = esp_get_free_internal_heap_size();
        if (free_internal < 30000) {
            ESP_LOGE(TAG, "Internal heap critically low (%u bytes), rebooting", (unsigned)free_internal);
            esp_restart();
        }

        ESP_LOGI(TAG, "Connecting to %s (heap: %u)", ws_url, (unsigned)free_internal);

        ws_connected = false;
        ws_authenticated = false;

        // Configure WebSocket client
        esp_websocket_client_config_t ws_cfg = {
            .uri = ws_url,
            .reconnect_timeout_ms = 0,  // Disable internal reconnect — outer loop handles retry
            .network_timeout_ms = 10000,
            .ping_interval_sec = 30,
            .buffer_size = 8192,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .headers = "ngrok-skip-browser-warning: true\r\n",
        };

        ws_client = esp_websocket_client_init(&ws_cfg);
        if (!ws_client) {
            ESP_LOGE(TAG, "Failed to init WebSocket client");
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }

        esp_websocket_register_events(ws_client, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL);

        esp_err_t err = esp_websocket_client_start(ws_client);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start WebSocket client: %d", err);
            esp_websocket_client_destroy(ws_client);
            ws_client = NULL;
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }

        // Wait for connection (up to 15 seconds)
        int wait_count = 0;
        while (!esp_websocket_client_is_connected(ws_client) && wait_count < 30) {
            vTaskDelay(500 / portTICK_PERIOD_MS);
            wait_count++;
        }

        if (!esp_websocket_client_is_connected(ws_client)) {
            ESP_LOGW(TAG, "Connection timeout");
            esp_websocket_client_stop(ws_client);
            esp_websocket_client_destroy(ws_client);
            ws_client = NULL;
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }

        // Connected — send auth immediately
        send_auth();

        // Main loop — just process incoming commands
        while (esp_websocket_client_is_connected(ws_client)) {
            process_queued_messages();
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }

        // Disconnected — clean up and reconnect
        ESP_LOGW(TAG, "Connection lost, cleaning up...");
        ws_connected = false;
        ws_authenticated = false;
        esp_websocket_client_stop(ws_client);
        esp_websocket_client_destroy(ws_client);
        ws_client = NULL;

        // Drain leftover messages
        ws_msg_t leftover;
        while (xQueueReceive(ws_msg_queue, &leftover, 0) == pdTRUE) {
            free(leftover.data);
        }

        ESP_LOGI(TAG, "Reconnecting in 5 seconds...");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
