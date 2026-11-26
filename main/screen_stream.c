/**
 * Screen streaming module - async broadcasting of screen frames
 * Uses a task to capture frames and send to all connected clients
 */
#include "screen_stream.h"
#include "display_capture.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char* TAG = "screen_stream";

#define FRAME_QUEUE_SIZE 2

// Client management
static httpd_req_t* clients[MAX_SCREEN_STREAM_CLIENTS];
static int active_clients = 0;
static SemaphoreHandle_t clients_mutex = NULL;

// Task control
static TaskHandle_t stream_task_handle = NULL;
static bool task_running = false;

// Frame interval in ms (10 FPS)
#define FRAME_INTERVAL_MS 100

static void remove_client_internal(httpd_req_t* req, bool from_task)
{
    if (!from_task) {
        if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            return;
        }
    }

    for (int i = 0; i < MAX_SCREEN_STREAM_CLIENTS; i++) {
        if (clients[i] == req) {
            // Signal completion to the HTTP server
            httpd_req_async_handler_complete(req);
            
            clients[i] = NULL;
            active_clients--;
            ESP_LOGI(TAG, "Removed screen stream client, handle: %p, slot: %d, active: %d", req, i, active_clients);
            break;
        }
    }

    if (!from_task) {
        xSemaphoreGive(clients_mutex);
    }
}

static void stream_task(void* pvParameters)
{
    ESP_LOGI(TAG, "Screen stream task started");

    while (task_running) {
        if (active_clients == 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Capture screen once for all clients
        uint8_t* png_buffer = NULL;
        size_t png_size = 0;
        esp_err_t err = display_capture_get_png(&png_buffer, &png_size);

        if (err == ESP_OK && png_buffer != NULL && png_size > 0) {
            if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                // Send same frame to all active clients
                for (int i = 0; i < MAX_SCREEN_STREAM_CLIENTS; i++) {
                    if (clients[i] != NULL) {
                        // Prepare frame header
                        char frame_header[64];
                        int header_len = snprintf(frame_header, sizeof(frame_header),
                            "--frame\r\nContent-Type: image/png\r\nContent-Length: %zu\r\n\r\n",
                            png_size);

                        // Send header
                        if (httpd_resp_send_chunk(clients[i], frame_header, header_len) != ESP_OK) {
                            remove_client_internal(clients[i], true);
                            continue;
                        }

                        // Send PNG data
                        if (httpd_resp_send_chunk(clients[i], (const char *)png_buffer, png_size) != ESP_OK) {
                            remove_client_internal(clients[i], true);
                            continue;
                        }

                        // Send footer
                        if (httpd_resp_send_chunk(clients[i], "\r\n", 2) != ESP_OK) {
                            remove_client_internal(clients[i], true);
                        }
                    }
                }
                xSemaphoreGive(clients_mutex);
            }
        }

        // Wait for next frame
        vTaskDelay(pdMS_TO_TICKS(FRAME_INTERVAL_MS));
    }

    ESP_LOGI(TAG, "Screen stream task ended");
    vTaskDelete(NULL);
}

esp_err_t screen_stream_init(void)
{
    ESP_LOGI(TAG, "Initializing screen stream");

    // Initialize client array
    memset(clients, 0, sizeof(clients));

    // Create mutex
    clients_mutex = xSemaphoreCreateMutex();
    if (clients_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create clients mutex");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t screen_stream_add_client(httpd_req_t *async_req)
{
    if (clients_mutex == NULL) {
        return ESP_FAIL;
    }

    if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex for adding client");
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_ERR_NOT_FOUND;
    for (int i = 0; i < MAX_SCREEN_STREAM_CLIENTS; i++) {
        if (clients[i] == NULL) {
            clients[i] = async_req;
            active_clients++;
            ESP_LOGI(TAG, "Added screen stream client, handle: %p, slot: %d, active: %d", async_req, i, active_clients);
            ret = ESP_OK;
            break;
        }
    }

    xSemaphoreGive(clients_mutex);

    // Start task if not running and we have clients
    if (ret == ESP_OK && active_clients == 1 && !task_running) {
        task_running = true;
        BaseType_t res = xTaskCreate(
            stream_task,
            "screen_stream",
            4096,
            NULL,
            3,
            &stream_task_handle
        );
        if (res != pdPASS) {
            ESP_LOGE(TAG, "Failed to create stream task");
            task_running = false;
            remove_client_internal(async_req, false);
            return ESP_FAIL;
        }
    }

    return ret;
}

void screen_stream_remove_client(httpd_req_t *async_req)
{
    remove_client_internal(async_req, false);

    // Stop task if no clients
    if (active_clients == 0 && task_running) {
        task_running = false;
        if (stream_task_handle != NULL) {
            vTaskDelete(stream_task_handle);
            stream_task_handle = NULL;
        }
    }
}

void screen_stream_cleanup(void)
{
    task_running = false;

    if (stream_task_handle != NULL) {
        vTaskDelete(stream_task_handle);
        stream_task_handle = NULL;
    }

    if (clients_mutex != NULL) {
        if (xSemaphoreTake(clients_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            for (int i = 0; i < MAX_SCREEN_STREAM_CLIENTS; i++) {
                if (clients[i] != NULL) {
                    httpd_req_async_handler_complete(clients[i]);
                    clients[i] = NULL;
                }
            }
            xSemaphoreGive(clients_mutex);
        }
        vSemaphoreDelete(clients_mutex);
        clients_mutex = NULL;
    }

    active_clients = 0;

    ESP_LOGI(TAG, "Screen stream cleaned up");
}
