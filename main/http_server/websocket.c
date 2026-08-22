#include <string.h>
#include <strings.h>

#include <stdint.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "websocket.h"
#include "websocket_log.h"
#include "websocket_api.h"
#include "http_server.h"
#include "websocket_internal.h"

static const char * TAG = "websocket";

typedef struct {
    int fd;
    WebSocketClientType type;
} ws_client_t;

static ws_client_t clients[MAX_WEBSOCKET_CLIENTS];
static int type_counts[WS_TYPE_MAX] = {0};
static SemaphoreHandle_t clients_mutex = NULL;
static httpd_handle_t server_handle = NULL;
static TaskHandle_t s_websocket_log_task_handle = NULL;

WEBSOCKET_STATIC bool websocket_origin_matches_host(const char *origin, const char *host)
{
    if (origin == NULL || host == NULL || host[0] == 0) {
        return false;
    }

    const char *authority = NULL;
    static const char http_prefix[] = "http://";
    static const char https_prefix[] = "https://";

    if (strncasecmp(origin, http_prefix, sizeof(http_prefix) - 1) == 0) {
        authority = origin + sizeof(http_prefix) - 1;
    } else if (strncasecmp(origin, https_prefix, sizeof(https_prefix) - 1) == 0) {
        authority = origin + sizeof(https_prefix) - 1;
    } else {
        return false;
    }

    size_t authority_len = strcspn(authority, "/?#");
    if (authority_len == 0 || authority[authority_len] != 0) {
        return false;
    }

    size_t host_len = strlen(host);
    return authority_len == host_len && strncasecmp(authority, host, host_len) == 0;
}


WEBSOCKET_STATIC bool websocket_has_free_slot(void)
{
    if (clients_mutex == NULL ||
        xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex while checking client capacity");
        return false;
    }

    int active_clients = 0;
    for (int i = 0; i < WS_TYPE_MAX; i++) {
        active_clients += type_counts[i];
    }

    xSemaphoreGive(clients_mutex);
    return active_clients < MAX_WEBSOCKET_CLIENTS;
}

WEBSOCKET_STATIC esp_err_t websocket_origin_is_allowed(httpd_req_t *req)
{
    size_t origin_len = httpd_req_get_hdr_value_len(req, "Origin");
    if (origin_len == 0) {
        // Non-browser clients such as websocat do not necessarily send Origin.
        return ESP_OK;
    }

    size_t host_len = httpd_req_get_hdr_value_len(req, "Host");
    if (origin_len >= WS_HANDSHAKE_HEADER_SIZE || host_len == 0 ||
        host_len >= WS_HANDSHAKE_HEADER_SIZE) {
        ESP_LOGW(TAG, "Rejecting WebSocket handshake with invalid Origin/Host length");
        return ESP_FAIL;
    }

    char origin[WS_HANDSHAKE_HEADER_SIZE];
    char host[WS_HANDSHAKE_HEADER_SIZE];
    if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) != ESP_OK ||
        httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK) {
        ESP_LOGW(TAG, "Rejecting WebSocket handshake with unreadable Origin/Host");
        return ESP_FAIL;
    }

    if (!websocket_origin_matches_host(origin, host)) {
        ESP_LOGW(TAG, "Rejecting cross-origin WebSocket handshake");
        return ESP_FAIL;
    }

    return ESP_OK;
}

void websocket_set_log_task_handle(TaskHandle_t task_handle)
{
    s_websocket_log_task_handle = task_handle;
}

int websocket_get_active_client_count(WebSocketClientType type)
{
    if (type >= 0 && type < WS_TYPE_MAX) return type_counts[type];
    return 0;
}

void websocket_log_notify(void)
{
    if (s_websocket_log_task_handle != NULL && type_counts[WS_TYPE_LOGS] > 0) {
        xTaskNotifyGive(s_websocket_log_task_handle);
    }
}

esp_err_t websocket_add_client(int fd, WebSocketClientType type)
{
    if (type < 0 || type >= WS_TYPE_MAX) {
        ESP_LOGE(TAG, "Cannot add WebSocket client with invalid type: %d", (int)type);
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex for adding client");
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_FAIL;
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (clients[i].fd == -1) {
            clients[i].fd = fd;
            clients[i].type = type;
            type_counts[type]++;

            ESP_LOGI(TAG, "Added WebSocket %s client, fd: %d, slot: %d, type_count: %d",
                     type == WS_TYPE_LOGS ? "log" : "api", fd, i,
                     type_counts[type]);

            ret = ESP_OK;
            if (type == WS_TYPE_LOGS && s_websocket_log_task_handle) {
                xTaskNotifyGive(s_websocket_log_task_handle);
            }
            break;
        }
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Max WebSocket clients reached, cannot add fd: %d", fd);
    }

    xSemaphoreGive(clients_mutex);
    return ret;
}

void websocket_remove_client(int fd)
{
    if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex for removing client");
        return;
    }

    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (clients[i].fd == fd) {
            WebSocketClientType type = clients[i].type;
            clients[i].fd = -1;
            clients[i].type = WS_TYPE_API;
            type_counts[type]--;

            ESP_LOGI(TAG, "Removed WebSocket %s client, fd: %d, slot: %d, type_count: %d",
                     type == WS_TYPE_LOGS ? "log" : "api", fd, i,
                     type_counts[type]);

            break;
        }
    }

    xSemaphoreGive(clients_mutex);
}

void websocket_send_to_client(int fd, httpd_ws_frame_t *pkt)
{
    if (server_handle == NULL || fd == -1)
        return;

    esp_err_t err = httpd_ws_send_frame_async(server_handle, fd, pkt);

    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "Send failed: fd=%d err=%s (%d) - removing client",
                 fd,
                 esp_err_to_name(err),
                 err);

        websocket_remove_client(fd);

        esp_err_t close_err = httpd_sess_trigger_close(server_handle, fd);
        if (close_err != ESP_OK) {
            ESP_LOGW(TAG,
                "Failed to trigger HTTP session close for fd=%d: %s (%d)",
                fd,
                esp_err_to_name(close_err),
                close_err);
        }
    }
}

void websocket_broadcast(WebSocketClientType type, httpd_ws_frame_t *pkt)
{
    if (server_handle == NULL) return;

    int fds[MAX_WEBSOCKET_CLIENTS];
    int n = 0;

    if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex for broadcast");
        return;
    }

    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (clients[i].fd != -1 && clients[i].type == type) {
            fds[n++] = clients[i].fd;
        }
    }

    xSemaphoreGive(clients_mutex);

    for (int i = 0; i < n; i++) {
        websocket_send_to_client(fds[i], pkt);
    }
}

void websocket_close_fn(httpd_handle_t hd, int fd)
{
    websocket_remove_client(fd);
    close(fd);
}

void websocket_init(httpd_handle_t server)
{
    if (clients_mutex == NULL) {
        clients_mutex = xSemaphoreCreateMutex();
    }
    if (clients_mutex == NULL ||
        xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to initialize WebSocket client state");
        return;
    }

    server_handle = server;
    memset(type_counts, 0, sizeof(type_counts));
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        clients[i].fd = -1;
        clients[i].type = WS_TYPE_API;
    }

    xSemaphoreGive(clients_mutex);
}

esp_err_t websocket_pre_handshake(httpd_req_t *req)
{
    if (websocket_origin_is_allowed(req) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN,
                            "Forbidden WebSocket origin");
        return ESP_FAIL;
    }

    if (is_network_allowed(req) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        return ESP_FAIL;
    }

    WebSocketClientType type = (WebSocketClientType)(uintptr_t)req->user_ctx;
    if (type < 0 || type >= WS_TYPE_MAX) {
        ESP_LOGE(TAG, "Rejecting WebSocket connection with invalid client type: %d",
                 type);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Invalid WebSocket endpoint");
        return ESP_FAIL;
    }

    if (!websocket_has_free_slot()) {
        ESP_LOGW(TAG, "Max WebSocket clients reached, rejecting handshake");
        httpd_resp_send_custom_err(req, "429 Too Many Requests", "Max WebSocket clients reached");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t websocket_post_handshake(httpd_req_t *req)
{
    WebSocketClientType type = (WebSocketClientType)(uintptr_t)req->user_ctx;
    int fd = httpd_req_to_sockfd(req);
    if (websocket_add_client(fd, type) != ESP_OK) {
        ESP_LOGE(TAG, "Unexpected failure adding client, fd: %d", fd);
        return ESP_FAIL;
    }

    if (type == WS_TYPE_API) {
        websocket_api_on_connect(fd);
    }

    return ESP_OK;
}

esp_err_t websocket_handler(httpd_req_t *req)
{
    // Handle WebSocket frame
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    // Get frame header to allow ESP-IDF to handle control frames (Ping/Pong/Close)
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        return ret;
    }

    // Inbound application data is ignored, but it must be drained to keep the
    // WebSocket stream synchronized. Never allocate based on a peer-provided
    // frame length.
    if (ws_pkt.len > 0) {
        if (ws_pkt.len > WS_MAX_WEBSOCKET_PAYLOAD_SIZE) {
            ESP_LOGW(TAG, "Rejecting oversized WebSocket frame: %zu bytes", ws_pkt.len);
            return ESP_ERR_INVALID_SIZE;
        }

        uint8_t buf[WS_MAX_WEBSOCKET_PAYLOAD_SIZE];
        ws_pkt.payload = buf;
        return httpd_ws_recv_frame(req, &ws_pkt, sizeof(buf));
    }

    return ESP_OK;
}
