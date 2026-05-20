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
#include "log_buffer.h"

#define WS_LOG_SCRATCH_SIZE 2048

static const char * TAG = "websocket";

typedef struct {
    int fd;
    uint32_t type;
} ws_client_t;

static ws_client_t clients[MAX_WEBSOCKET_CLIENTS];
static int type_counts[WS_TYPE_MAX] = {0};
static SemaphoreHandle_t clients_mutex = NULL;
static httpd_handle_t server_handle = NULL;
static TaskHandle_t s_websocket_log_task_handle = NULL;

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
    if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex for adding client");
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_FAIL;
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (clients[i].fd == -1) {
            clients[i].fd = fd;
            clients[i].type = type;
            if (type >= 0 && type < WS_TYPE_MAX) type_counts[type]++;

            ESP_LOGI(TAG, "Added WebSocket %s client, fd: %d, slot: %d", type == WS_TYPE_LOGS ? "log" : "api", fd, i);
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
            WebSocketClientType type = (WebSocketClientType)clients[i].type;
            clients[i].fd = -1;
            clients[i].type = 0;
            if (type >= 0 && type < WS_TYPE_MAX) type_counts[type]--;

            ESP_LOGI(TAG, "Removed WebSocket %s client, fd: %d, slot: %d", type == WS_TYPE_LOGS ? "log" : "api", fd, i);
            break;
        }
    }

    xSemaphoreGive(clients_mutex);
}

void websocket_send_to_client(int fd, httpd_ws_frame_t *pkt)
{
    if (server_handle == NULL || fd == -1) return;
    if (httpd_ws_send_frame_async(server_handle, fd, pkt) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send WebSocket frame to fd: %d", fd);
    }
}

void websocket_broadcast(WebSocketClientType type, httpd_ws_frame_t *pkt)
{
    if (server_handle == NULL) return;

    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (clients[i].fd != -1 && (clients[i].type == type)) {
            websocket_send_to_client(clients[i].fd, pkt);
        }
    }
}

void websocket_close_fn(httpd_handle_t hd, int fd)
{
    websocket_remove_client(fd);
    close(fd);
}

void websocket_init(httpd_handle_t server)
{
    server_handle = server;
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        clients[i].fd = -1;
        clients[i].type = 0;
    }

    if (clients_mutex == NULL) {
        clients_mutex = xSemaphoreCreateMutex();
    }
}

esp_err_t websocket_handler(httpd_req_t *req)
{
    // Detect handshake by checking for the "Upgrade" header
    char upgrade_hdr[16];
    if (httpd_req_get_hdr_value_str(req, "Upgrade", upgrade_hdr, sizeof(upgrade_hdr)) == ESP_OK &&
        strcasecmp(upgrade_hdr, "websocket") == 0) {

        if (is_network_allowed(req) != ESP_OK) {
            return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
        }

        int active_clients = 0;
        for (int i = 0; i < WS_TYPE_MAX; i++) active_clients += type_counts[i];
        if (active_clients >= MAX_WEBSOCKET_CLIENTS) {
            ESP_LOGE(TAG, "Max WebSocket clients reached, rejecting new connection");
            return httpd_resp_send_custom_err(req, "429 Too Many Requests", "Max WebSocket clients reached");
        }

        uint32_t type = (uint32_t)(uintptr_t)req->user_ctx;
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

    // Handle WebSocket frame
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    // Get frame header to allow ESP-IDF to handle control frames (Ping/Pong/Close)
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        return ret;
    }

    // If there's a payload, drain it
    if (ws_pkt.len > 0) {
        uint8_t *buf = (uint8_t *)calloc(1, ws_pkt.len + 1);
        if (buf) {
            ws_pkt.payload = buf;
            ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
            free(buf);
            return ret;
        }
    }

    return ESP_OK;
}
