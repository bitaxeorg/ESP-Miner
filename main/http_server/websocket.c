#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "websocket.h"
#include "http_server.h"

static const char * TAG = "websocket";

static QueueHandle_t log_queue = NULL;
static int clients[MAX_WEBSOCKET_CLIENTS];

int log_to_queue(const char *format, va_list args)
{
    va_list args_copy;
    va_copy(args_copy, args);

    // Calculate the required buffer size
    int needed_size = vsnprintf(NULL, 0, format, args_copy) + 1;
    va_end(args_copy);

    // Allocate the buffer dynamically
    char *log_buffer = (char *)calloc(needed_size + 2, sizeof(char)); // +2 for \n and \0
    if (log_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for log buffer");
        return 0;
    }

    // Format the string into the allocated buffer
    va_copy(args_copy, args);
    vsnprintf(log_buffer, needed_size, format, args_copy);
    va_end(args_copy);

    // Ensure the log message ends with a newline
    size_t len = strlen(log_buffer);
    if (len > 0 && log_buffer[len - 1] != '\n') {
        log_buffer[len] = '\n';
        log_buffer[len + 1] = '\0';
        len++;
    }

    // Print to standard output
    printf("%s", log_buffer);

    // Send to queue for WebSocket broadcasting
    if (xQueueSendToBack(log_queue, (void*)&log_buffer, (TickType_t)0) != pdPASS) {
        ESP_LOGW(TAG, "Failed to send log to queue, freeing buffer");
        free((void*)log_buffer);
    }

    return 0;
}

static void init_clients(void)
{
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        clients[i] = -1;
    }
}

static esp_err_t add_client(int fd)
{
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (clients[i] == -1) {
            clients[i] = fd;
            ESP_LOGI(TAG, "Added WebSocket client, fd: %d, slot: %d", fd, i);
            return ESP_OK;
        }
    }
    ESP_LOGE(TAG, "Max WebSocket clients reached, cannot add fd: %d", fd);
    return ESP_FAIL;
}

static void remove_client(int fd)
{
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (clients[i] == fd) {
            clients[i] = -1;
            ESP_LOGI(TAG, "Removed WebSocket client, fd: %d, slot: %d", fd, i);
            return;
        }
    }
}

static int has_active_clients(void)
{
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (clients[i] != -1) {
            return 1;
        }
    }
    return 0;
}

void websocket_close_fn(httpd_handle_t hd, int fd)
{
    ESP_LOGI(TAG, "WebSocket client disconnected, fd: %d", fd);
    remove_client(fd);
    if (!has_active_clients()) {
        esp_log_set_vprintf(vprintf);
    }
}

esp_err_t websocket_handler(httpd_req_t *req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "WebSocket handshake done, new connection opened, fd: %d", fd);
        esp_err_t ret = add_client(fd);
        if (ret != ESP_OK) {
            return httpd_resp_send_custom_err(req, "429 Too Many Requests", "Max WebSocket clients reached");
        }
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t buf[128] = {0};
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = buf;
    ws_pkt.len = 128;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 128);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WebSocket frame receive failed: %s", esp_err_to_name(ret));
        remove_client(httpd_req_to_sockfd(req));
        if (!has_active_clients()) {
            esp_log_set_vprintf(vprintf);
        }
        return ret;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        ESP_LOGI(TAG, "WebSocket close frame received, fd: %d", httpd_req_to_sockfd(req));
        remove_client(httpd_req_to_sockfd(req));
        if (!has_active_clients()) {
            esp_log_set_vprintf(vprintf);
        }
        return ESP_OK;
    }

    return ESP_OK;
}

void websocket_task(void *pvParameters)
{
    ESP_LOGI(TAG, "websocket_task starting");
    log_queue = xQueueCreate(MESSAGE_QUEUE_SIZE, sizeof(char*));
    httpd_handle_t https_handle = (httpd_handle_t)pvParameters;
    init_clients();

    while (true) {
        char *message;
        if (xQueueReceive(log_queue, &message, (TickType_t) portMAX_DELAY) != pdPASS) {
            if (message != NULL) {
                free((void*)message);
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }

        if (!has_active_clients()) {
            free((void*)message);
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
            int client_fd = clients[i];
            if (client_fd != -1) {
                httpd_ws_frame_t ws_pkt;
                memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
                ws_pkt.payload = (uint8_t *)message;
                ws_pkt.len = strlen(message);
                ws_pkt.type = HTTPD_WS_TYPE_TEXT;

                if (httpd_ws_send_frame_async(https_handle, client_fd, &ws_pkt) != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to send WebSocket frame to fd: %d", client_fd);
                    remove_client(client_fd);
                }
            }
        }

        free((void*)message);
    }
}
