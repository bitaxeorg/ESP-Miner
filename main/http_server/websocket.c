#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "websocket.h"
#include "http_server.h"
#include "log_buffer.h"

#define WS_LOG_SCRATCH_SIZE 2048

static const char * TAG = "websocket";

static int clients[MAX_WEBSOCKET_CLIENTS];
static int active_clients = 0;
static SemaphoreHandle_t clients_mutex = NULL;
static TaskHandle_t s_websocket_task_handle = NULL;

void websocket_log_notify(void)
{
    if (s_websocket_task_handle != NULL && active_clients > 0) {
        xTaskNotifyGive(s_websocket_task_handle);
    }
}

static esp_err_t add_client(int fd)
{
    if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex for adding client");
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_FAIL;
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (clients[i] == -1) {
            clients[i] = fd;
            active_clients++;
            ESP_LOGI(TAG, "Added WebSocket client, fd: %d, slot: %d", fd, i);
            ret = ESP_OK;
            if (s_websocket_task_handle) xTaskNotifyGive(s_websocket_task_handle);
            break;
        }
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Max WebSocket clients reached, cannot add fd: %d", fd);
    }

    xSemaphoreGive(clients_mutex);
    return ret;
}

static void remove_client(int fd)
{
    if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex for removing client");
        return;
    }

    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (clients[i] == fd) {
            clients[i] = -1;
            active_clients--;
            ESP_LOGI(TAG, "Removed WebSocket client, fd: %d, slot: %d", fd, i);
            break;
        }
    }

    xSemaphoreGive(clients_mutex);
}

void websocket_close_fn(httpd_handle_t hd, int fd)
{
    ESP_LOGI(TAG, "WebSocket client disconnected, fd: %d", fd);
    remove_client(fd);
    close(fd);
}

esp_err_t websocket_handler(httpd_req_t *req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    if (req->method == HTTP_GET) {
        if (active_clients >= MAX_WEBSOCKET_CLIENTS) {
            ESP_LOGE(TAG, "Max WebSocket clients reached, rejecting new connection");
            esp_err_t ret = httpd_resp_send_custom_err(req, "429 Too Many Requests", "Max WebSocket clients reached");
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send error response: %s", esp_err_to_name(ret));
            }
            int fd = httpd_req_to_sockfd(req);
            if (fd >= 0) {
                ESP_LOGI(TAG, "Closing fd: %d for rejected connection", fd);
                httpd_sess_trigger_close(req->handle, fd);
            }
            return ret;
        }

        int fd = httpd_req_to_sockfd(req);
        esp_err_t ret = add_client(fd);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Unexpected failure adding client, fd: %d", fd);
            ret = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unexpected failure adding client");
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send error response: %s", esp_err_to_name(ret));
            }
            ESP_LOGI(TAG, "Closing fd: %d for failed client addition", fd);
            httpd_sess_trigger_close(req->handle, fd);
            return ret;
        }
        ESP_LOGI(TAG, "WebSocket handshake successful, fd: %d", fd);
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK || ws_pkt.len == 0) {
        ESP_LOGE(TAG, "Failed to get WebSocket frame size: %s", esp_err_to_name(ret));
        remove_client(httpd_req_to_sockfd(req));
        return ret;
    }

    uint8_t *buf = (uint8_t *)calloc(ws_pkt.len, sizeof(uint8_t));
    if (buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for WebSocket frame buffer");
        remove_client(httpd_req_to_sockfd(req));
        return ESP_FAIL;
    }

    ws_pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WebSocket frame receive failed: %s", esp_err_to_name(ret));
        free(buf);
        remove_client(httpd_req_to_sockfd(req));
        return ret;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        ESP_LOGI(TAG, "WebSocket close frame received, fd: %d", httpd_req_to_sockfd(req));
        free(buf);
        remove_client(httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    // TODO: Handle incoming packets here
    free(buf);
    return ESP_OK;
}

void websocket_task(void *pvParameters)
{
    ESP_LOGI(TAG, "websocket_task starting");
    httpd_handle_t https_handle = (httpd_handle_t)pvParameters;
    s_websocket_task_handle = xTaskGetCurrentTaskHandle();

    memset(clients, -1, sizeof(clients));
    clients_mutex = xSemaphoreCreateMutex();

    uint64_t last_read_abs = log_buffer_get_total_written();
    char *scratch_buf = (char *)malloc(WS_LOG_SCRATCH_SIZE);

    while (true) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));

        if (active_clients == 0) {
            last_read_abs = log_buffer_get_total_written();
            continue;
        }

        while (true) {
            size_t read_len = log_buffer_read_absolute(&last_read_abs, scratch_buf, WS_LOG_SCRATCH_SIZE);
            if (read_len == 0) {
                break;
            }

            for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
                int client_fd = clients[i];
                if (client_fd != -1) {
                    httpd_ws_frame_t ws_pkt;
                    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
                    ws_pkt.payload = (uint8_t *)scratch_buf;
                    ws_pkt.len = read_len;
                    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

                    if (httpd_ws_send_frame_async(https_handle, client_fd, &ws_pkt) != ESP_OK) {
                        ESP_LOGW(TAG, "Failed to send WebSocket frame to fd: %d", client_fd);
                        remove_client(client_fd);
                    }
                }
            }
        }
    }

    if (scratch_buf) free(scratch_buf);
}
