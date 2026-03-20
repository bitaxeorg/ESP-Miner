/**
 * Gateway Task — local HTTP API handlers.
 *
 * In server-driven (dumb proxy) mode the gateway holds no peer state locally.
 * The HTTP API only exposes connection status and the current scan state.
 */

#include "gateway_task.h"
#include "ws_client_task.h"
#include "global_state.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "gateway";

static GlobalState *g_state = NULL;

void gateway_init(void *state_ptr)
{
    GlobalState *state = (GlobalState *)state_ptr;
    g_state = state;
    memset(&state->GATEWAY_MODULE.scan_state, 0, sizeof(state->GATEWAY_MODULE.scan_state));
    ESP_LOGI(TAG, "Gateway module initialized (server-driven mode)");
}

// GET /api/gateway — connection status + scan state
esp_err_t GET_gateway_status(httpd_req_t *req)
{
    if (!g_state) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Gateway not initialized");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "mode", "server-driven");

    cJSON *cloud = cJSON_CreateObject();
    bool connected     = ws_client_is_connected();
    bool authenticated = ws_client_is_authenticated();
    const char *client_id = ws_client_get_client_id();
    cJSON_AddBoolToObject(cloud, "connected",     connected);
    cJSON_AddBoolToObject(cloud, "authenticated", authenticated);
    cJSON_AddStringToObject(cloud, "status",
        authenticated ? "authenticated" :
        connected     ? "connected"     : "disconnected");
    if (client_id && client_id[0])
        cJSON_AddStringToObject(cloud, "clientId", client_id);
    cJSON_AddItemToObject(root, "cloudConnection", cloud);

    cJSON *scan = cJSON_CreateObject();
    cJSON_AddBoolToObject(scan, "scanning", g_state->GATEWAY_MODULE.scan_state.scanning);
    cJSON_AddItemToObject(root, "scan", scan);

    const char *json_str = cJSON_Print(root);
    httpd_resp_sendstr(req, json_str);
    free((void *)json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// PATCH /api/gateway — no-op in server-driven mode
esp_err_t PATCH_gateway_settings(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"mode\":\"server-driven\"}");
    return ESP_OK;
}

// POST /api/gateway/test — no-op in server-driven mode (server drives polling)
esp_err_t POST_gateway_test(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"results\":[],\"note\":\"Polling is server-driven\"}");
    return ESP_OK;
}
