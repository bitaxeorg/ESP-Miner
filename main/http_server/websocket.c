#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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

static void websocket_drop_client(int fd);

static const char *websocket_type_name(WebSocketClientType type)
{
    return type == WS_TYPE_LOGS ? "log" : "api";
}

static bool websocket_type_is_valid(WebSocketClientType type)
{
    return type >= 0 && type < WS_TYPE_MAX;
}

static bool websocket_take_clients_mutex(void)
{
    if (clients_mutex == NULL) {
        ESP_LOGE(TAG, "WebSocket client mutex is not initialized");
        return false;
    }
    if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire WebSocket client mutex");
        return false;
    }
    return true;
}

static void websocket_forget_client_locked(int fd)
{
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (clients[i].fd == fd) {
            WebSocketClientType type = (WebSocketClientType)clients[i].type;
            clients[i].fd = -1;
            clients[i].type = 0;

            if (websocket_type_is_valid(type) && type_counts[type] > 0) {
                type_counts[type]--;
            }

            return;
        }
    }
}

static void websocket_prune_inactive_clients(void)
{
    if (server_handle == NULL) {
        return;
    }

    int client_fds[MAX_WEBSOCKET_CLIENTS];
    int client_count = 0;

    if (!websocket_take_clients_mutex()) {
        return;
    }

    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        int fd = clients[i].fd;
        if (fd == -1) {
            continue;
        }

        client_fds[client_count++] = fd;
    }

    xSemaphoreGive(clients_mutex);

    for (int i = 0; i < client_count; i++) {
        int fd = client_fds[i];
        if (httpd_ws_get_fd_info(server_handle, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
            ESP_LOGW(TAG, "Pruning stale WebSocket client, fd: %d", fd);
            websocket_drop_client(fd);
        }
    }
}

static int websocket_get_total_client_count(void)
{
    int active_clients = MAX_WEBSOCKET_CLIENTS;

    websocket_prune_inactive_clients();

    if (!websocket_take_clients_mutex()) {
        return active_clients;
    }

    active_clients = 0;
    for (int i = 0; i < WS_TYPE_MAX; i++) {
        active_clients += type_counts[i];
    }

    xSemaphoreGive(clients_mutex);
    return active_clients;
}

static void websocket_drop_client(int fd)
{
    if (fd == -1) {
        return;
    }

    websocket_remove_client(fd);
}

void websocket_set_log_task_handle(TaskHandle_t task_handle)
{
    s_websocket_log_task_handle = task_handle;
}

int websocket_get_active_client_count(WebSocketClientType type)
{
    if (!websocket_type_is_valid(type)) {
        return 0;
    }

    int count = 0;
    websocket_prune_inactive_clients();

    if (!websocket_take_clients_mutex()) {
        return count;
    }

    count = type_counts[type];

    xSemaphoreGive(clients_mutex);
    return count;
}

void websocket_log_notify(void)
{
    if (s_websocket_log_task_handle != NULL && websocket_get_active_client_count(WS_TYPE_LOGS) > 0) {
        xTaskNotifyGive(s_websocket_log_task_handle);
    }
}

esp_err_t websocket_add_client(int fd, WebSocketClientType type)
{
    if (!websocket_type_is_valid(type)) {
        ESP_LOGE(TAG, "Invalid WebSocket client type: %d", type);
        return ESP_ERR_INVALID_ARG;
    }

    websocket_prune_inactive_clients();

    if (!websocket_take_clients_mutex()) {
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_FAIL;
    int active_clients = 0;
    int updated_slot = -1;
    int added_slot = -1;
    for (int i = 0; i < WS_TYPE_MAX; i++) {
        active_clients += type_counts[i];
    }

    if (active_clients >= MAX_WEBSOCKET_CLIENTS) {
        ESP_LOGE(TAG, "Max WebSocket clients reached, cannot add fd: %d", fd);
        xSemaphoreGive(clients_mutex);
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (clients[i].fd == fd) {
            WebSocketClientType old_type = (WebSocketClientType)clients[i].type;
            if (old_type != type) {
                if (websocket_type_is_valid(old_type) && type_counts[old_type] > 0) {
                    type_counts[old_type]--;
                }
                type_counts[type]++;
                clients[i].type = type;
            }
            updated_slot = i;
            ret = ESP_OK;
            break;
        }

        if (clients[i].fd == -1) {
            clients[i].fd = fd;
            clients[i].type = type;
            type_counts[type]++;

            added_slot = i;
            ret = ESP_OK;
            if (type == WS_TYPE_LOGS && s_websocket_log_task_handle) {
                xTaskNotifyGive(s_websocket_log_task_handle);
            }
            break;
        }
    }
    xSemaphoreGive(clients_mutex);

    if (updated_slot >= 0) {
        ESP_LOGI(TAG, "Updated WebSocket %s client, fd: %d, slot: %d", websocket_type_name(type), fd, updated_slot);
    } else if (added_slot >= 0) {
        ESP_LOGI(TAG, "Added WebSocket %s client, fd: %d, slot: %d", websocket_type_name(type), fd, added_slot);
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Max WebSocket clients reached, cannot add fd: %d", fd);
    }

    return ret;
}

void websocket_remove_client(int fd)
{
    if (!websocket_take_clients_mutex()) {
        return;
    }

    websocket_forget_client_locked(fd);

    xSemaphoreGive(clients_mutex);
}

void websocket_send_to_client(int fd, httpd_ws_frame_t *pkt)
{
    if (server_handle == NULL || fd == -1) return;

    httpd_ws_client_info_t client_info = httpd_ws_get_fd_info(server_handle, fd);
    if (client_info != HTTPD_WS_CLIENT_WEBSOCKET) {
        ESP_LOGW(TAG, "Dropping stale WebSocket client before send, fd: %d", fd);
        websocket_drop_client(fd);
        return;
    }

    esp_err_t ret = httpd_ws_send_frame_async(server_handle, fd, pkt);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send WebSocket frame to fd %d: %s", fd, esp_err_to_name(ret));
        websocket_drop_client(fd);
    }
}

void websocket_broadcast(WebSocketClientType type, httpd_ws_frame_t *pkt)
{
    if (server_handle == NULL) return;

    int client_fds[MAX_WEBSOCKET_CLIENTS];
    int client_count = 0;

    websocket_prune_inactive_clients();

    if (!websocket_take_clients_mutex()) {
        return;
    }

    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (clients[i].fd != -1 && (clients[i].type == type)) {
            client_fds[client_count++] = clients[i].fd;
        }
    }

    xSemaphoreGive(clients_mutex);

    for (int i = 0; i < client_count; i++) {
        websocket_send_to_client(client_fds[i], pkt);
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
    if (clients_mutex == NULL) {
        clients_mutex = xSemaphoreCreateMutex();
    }

    if (!websocket_take_clients_mutex()) {
        return;
    }

    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        clients[i].fd = -1;
        clients[i].type = 0;
    }
    for (int i = 0; i < WS_TYPE_MAX; i++) {
        type_counts[i] = 0;
    }

    xSemaphoreGive(clients_mutex);
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

        if (websocket_get_total_client_count() >= MAX_WEBSOCKET_CLIENTS) {
            ESP_LOGE(TAG, "Max WebSocket clients reached, rejecting new connection");
            return httpd_resp_send_custom_err(req, "429 Too Many Requests", "Max WebSocket clients reached");
        }

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

    // Handle WebSocket frame
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    // Get frame header to allow ESP-IDF to handle control frames (Ping/Pong/Close)
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        websocket_drop_client(httpd_req_to_sockfd(req));
        return ret;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        websocket_drop_client(httpd_req_to_sockfd(req));
        return ESP_OK;
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
