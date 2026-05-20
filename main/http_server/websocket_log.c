#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "websocket.h"
#include "websocket_log.h"
#include "log_buffer.h"

static const char *TAG = "websocket_log";

#define WS_LOG_CHUNK_SIZE 4096

void websocket_log_task(void *pvParameters)
{
    ESP_LOGI(TAG, "websocket_log_task starting");

    // Initialize position to the current end of buffer to start with "live" logs
    uint64_t last_read_abs = log_buffer_get_total_written();
    char *scratch_buf = (char *)malloc(WS_LOG_CHUNK_SIZE);
    if (!scratch_buf) {
        ESP_LOGE(TAG, "Failed to allocate scratch buffer");
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        // Wait for notification from log_buffer_vprintf or timeout
        // Notification is sent whenever websocket_log_notify() is called
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));

        // Only drain if we actually have clients interested
        if (websocket_get_active_client_count(WS_TYPE_LOGS) == 0) {
            // Keep up with current pointer so we don't dump everything when someone connects
            last_read_abs = log_buffer_get_total_written();
            continue;
        }

        // Catch up with all new logs in the buffer
        while (true) {
            size_t read_len = log_buffer_read_absolute(&last_read_abs, scratch_buf, WS_LOG_CHUNK_SIZE);
            if (read_len == 0) {
                break;
            }

            httpd_ws_frame_t ws_pkt;
            memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
            ws_pkt.payload = (uint8_t *)scratch_buf;
            ws_pkt.len = read_len;
            ws_pkt.type = HTTPD_WS_TYPE_TEXT;

            websocket_broadcast(WS_TYPE_LOGS, &ws_pkt);
        }
    }

    free(scratch_buf);
    vTaskDelete(NULL);
}
