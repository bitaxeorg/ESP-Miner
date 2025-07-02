#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "http_server.h"

#define MESSAGE_QUEUE_SIZE (128)

static const char * TAG = "websocket";

static QueueHandle_t log_queue = NULL;
static int fd = -1;

int log_to_queue(const char * format, va_list args)
{
    va_list args_copy;
    va_copy(args_copy, args);

    // Calculate the required buffer size
    int needed_size = vsnprintf(NULL, 0, format, args_copy) + 1;
    va_end(args_copy);

    // Allocate the buffer dynamically
    char * log_buffer = (char *) calloc(needed_size + 2, sizeof(char));  // +2 for potential \n and \0
    if (log_buffer == NULL) {
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

    if (xQueueSendToBack(log_queue, (void*)&log_buffer, (TickType_t) 0) != pdPASS) {
        if (log_buffer != NULL) {
            free((void*)log_buffer);
        }
    }

    return 0;
}

esp_err_t websocket_handler(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        fd = httpd_req_to_sockfd(req);
        esp_log_set_vprintf(log_to_queue);
        return ESP_OK;
    }
    return ESP_OK;
}

void websocket_task(void * pvParameters)
{
    ESP_LOGI(TAG, "websocket_task starting");
    log_queue = xQueueCreate(MESSAGE_QUEUE_SIZE, sizeof(char*));

    httpd_handle_t server = pvParameters;
    while (true)
    {
        char *message;
        if (xQueueReceive(log_queue, &message, (TickType_t) portMAX_DELAY) != pdPASS) {
            if (message != NULL) {
                free((void*)message);
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }

        if (fd == -1) {
            free((void*)message);
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        // Prepare the WebSocket frame
        httpd_ws_frame_t ws_pkt;
        memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
        ws_pkt.payload = (uint8_t *)message;
        ws_pkt.len = strlen(message);
        ws_pkt.type = HTTPD_WS_TYPE_TEXT;

        // Ensure server and fd are valid
        if (fd >= 0) {
            // Send the WebSocket frame asynchronously
            if (httpd_ws_send_frame_async(server, fd, &ws_pkt) != ESP_OK) {
                esp_log_set_vprintf(vprintf);
            }
        }

        // Free the allocated buffer
        free((void*)message);
    }
}
