/**
 * Gateway Task — HTTP API handlers only.
 *
 * The old self-driven polling loop is removed. All polling and command
 * execution is now handled by ws_client_task in server-driven mode.
 *
 * This file retains the HTTP API handlers for the local AxeOS web UI:
 *   GET  /api/gateway       — gateway status + peer stats
 *   PATCH /api/gateway      — (legacy, limited)
 *   POST /api/gateway/test  — test connectivity to peers
 */

#include "gateway_task.h"
#include "miner_adapter.h"
#include "ws_client_task.h"
#include "global_state.h"
#include "nvs_config.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "gateway";

// Module-level pointer to global state
static GlobalState *g_state = NULL;

// Called by main.c to set the global state pointer (no task needed)
void gateway_init(void *state_ptr)
{
    GlobalState *state = (GlobalState *)state_ptr;
    g_state = state;
    memset(&state->GATEWAY_MODULE.SCAN_STATE, 0, sizeof(ScanState));
    state->GATEWAY_MODULE.peer_count = 0;
    state->GATEWAY_MODULE.is_polling = false;
    ESP_LOGI(TAG, "Gateway module initialized (server-driven mode)");
}

// ---- HTTP API Handlers ----

// GET /api/gateway - returns gateway status + all peer stats
esp_err_t GET_gateway_status(httpd_req_t *req)
{
    if (!g_state) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Gateway not initialized");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");

    GatewayModule *gw = &g_state->GATEWAY_MODULE;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "enabled", true); // Always enabled in server-driven mode
    cJSON_AddBoolToObject(root, "isPolling", gw->is_polling);
    cJSON_AddNumberToObject(root, "peerCount", gw->peer_count);
    cJSON_AddStringToObject(root, "mode", "server-driven");

    cJSON *cloud = cJSON_CreateObject();
    bool connected = ws_client_is_connected();
    bool authenticated = ws_client_is_authenticated();
    const char *client_id = ws_client_get_client_id();
    cJSON_AddBoolToObject(cloud, "connected", connected);
    cJSON_AddBoolToObject(cloud, "authenticated", authenticated);
    cJSON_AddStringToObject(cloud, "status",
        authenticated ? "authenticated" :
        connected     ? "connected" :
                        "disconnected");
    if (client_id && client_id[0] != '\0')
        cJSON_AddStringToObject(cloud, "clientId", client_id);
    cJSON_AddItemToObject(root, "cloudConnection", cloud);

    cJSON *peers_array = cJSON_CreateArray();
    for (int i = 0; i < gw->peer_count; i++) {
        PeerMinerState *peer = &gw->peers[i];
        cJSON *p = cJSON_CreateObject();
        cJSON_AddStringToObject(p, "ip", peer->ip);
        cJSON_AddStringToObject(p, "mac", peer->mac_addr);
        cJSON_AddStringToObject(p, "type", miner_type_to_string(peer->type));
        cJSON_AddStringToObject(p, "status",
            peer->status == PEER_STATUS_ONLINE ? "online" :
            peer->status == PEER_STATUS_OFFLINE ? "offline" :
            peer->status == PEER_STATUS_ERROR ? "error" : "unknown");
        cJSON_AddStringToObject(p, "hostname", peer->hostname);
        cJSON_AddStringToObject(p, "asicModel", peer->asic_model);
        cJSON_AddNumberToObject(p, "hashrate", peer->hashrate);
        cJSON_AddNumberToObject(p, "temp", peer->temp);
        cJSON_AddNumberToObject(p, "power", peer->power);
        cJSON_AddNumberToObject(p, "voltage", peer->voltage);
        cJSON_AddNumberToObject(p, "frequency", peer->frequency);
        cJSON_AddNumberToObject(p, "sharesAccepted", peer->shares_accepted);
        cJSON_AddNumberToObject(p, "sharesRejected", peer->shares_rejected);
        cJSON_AddNumberToObject(p, "uptimeSeconds", peer->uptime_seconds);
        if (peer->device_model[0] != '\0')
            cJSON_AddStringToObject(p, "deviceModel", peer->device_model);
        if (peer->temp_avg > 0) cJSON_AddNumberToObject(p, "tempAvg", peer->temp_avg);
        if (peer->temp_max > 0) cJSON_AddNumberToObject(p, "tempMax", peer->temp_max);

        if (peer->last_seen > 0) {
            int64_t now = esp_timer_get_time();
            cJSON_AddNumberToObject(p, "lastSeenSecondsAgo", (now - peer->last_seen) / 1000000);
        }

        cJSON_AddItemToArray(peers_array, p);
    }
    cJSON_AddItemToObject(root, "peers", peers_array);

    // Scan state
    cJSON *scan = cJSON_CreateObject();
    cJSON_AddBoolToObject(scan, "scanning", gw->SCAN_STATE.scanning);
    cJSON_AddBoolToObject(scan, "complete", gw->SCAN_STATE.complete);
    cJSON_AddNumberToObject(scan, "resultCount", gw->SCAN_STATE.result_count);
    cJSON_AddItemToObject(root, "scan", scan);

    const char *json_str = cJSON_Print(root);
    httpd_resp_sendstr(req, json_str);
    free((void *)json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// PATCH /api/gateway - limited settings (no NVS peer management in server-driven mode)
esp_err_t PATCH_gateway_settings(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"mode\":\"server-driven\",\"note\":\"Peers managed by cloud\"}");
    return ESP_OK;
}

// POST /api/gateway/test - test connectivity to current peers
esp_err_t POST_gateway_test(httpd_req_t *req)
{
    if (!g_state) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"results\":[]}");
        return ESP_OK;
    }

    GatewayModule *gw = &g_state->GATEWAY_MODULE;

    cJSON *root = cJSON_CreateObject();
    cJSON *results = cJSON_CreateArray();

    for (int i = 0; i < gw->peer_count; i++) {
        PeerMinerState test_peer;
        memset(&test_peer, 0, sizeof(test_peer));

        cJSON *r = cJSON_CreateObject();
        cJSON_AddStringToObject(r, "ip", gw->peers[i].ip);

        bool ok = poll_miner(gw->peers[i].ip, gw->peers[i].type, &test_peer);
        cJSON_AddBoolToObject(r, "reachable", ok);
        if (ok) {
            cJSON_AddStringToObject(r, "hostname", test_peer.hostname);
            cJSON_AddStringToObject(r, "asicModel", test_peer.asic_model);
            cJSON_AddNumberToObject(r, "hashrate", test_peer.hashrate);
            cJSON_AddStringToObject(r, "type", miner_type_to_string(test_peer.type));
            cJSON_AddStringToObject(r, "mac", test_peer.mac_addr);
        }

        cJSON_AddItemToArray(results, r);
    }

    cJSON_AddItemToObject(root, "results", results);

    const char *json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    free((void *)json_str);
    cJSON_Delete(root);
    return ESP_OK;
}
