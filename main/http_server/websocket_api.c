#include <string.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "websocket_api.h"
#include "websocket.h"
#include "global_state.h"
#include "system_api_json.h"
#include "cjson_utils.h"

#define WEBSOCKET_API_RATE_LIMIT_MS 500

static const char *TAG = "websocket_api";
static GlobalState *GLOBAL_STATE = NULL;

static int prebuffer_len = 256;

/**
 * @brief Builds the JSON update message and broadcasts it.
 * 
 * @param last_state The previous full JSON state (will be used for diffing).
 * @param fd Client file descriptor (-1 for broadcast).
 * @return cJSON* The new full JSON state that should track current reality.
 */
static cJSON* process_and_send_update(cJSON *last_state, int fd)
{
    cJSON *current_full = system_api_get_full_json(GLOBAL_STATE);
    if (!current_full) return last_state; // Keep last state if we failed to generate new one

    cJSON *diff = cJSON_GetDiff(last_state, current_full);
    
    // If no changes found, nothing to send
    if (diff == NULL) {
        if (fd != -1) {
             // For specific client connection, we MUST send something.
             // If Diff against null, it will be the full house. 
             // If last_state was null, diff won't be null.
        } else {
            return current_full;
        }
    }

    if (diff) {
        cJSON *msg = cJSON_CreateObject();
        if (msg) {
            cJSON_AddStringToObject(msg, "event", "update");
            cJSON_AddItemToObject(msg, "data", diff);

            const char * json_str = cJSON_PrintBuffered(msg, prebuffer_len, false);
            if (json_str != NULL) {
                int len = strlen(json_str);
                if (len > prebuffer_len) prebuffer_len = len * 1.2;

                httpd_ws_frame_t ws_pkt;
                memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
                ws_pkt.payload = (uint8_t *)json_str;
                ws_pkt.len = strlen(json_str);
                ws_pkt.type = HTTPD_WS_TYPE_TEXT;

                if (fd == -1) {
                    websocket_broadcast(WS_TYPE_API, &ws_pkt);
                } else {
                    websocket_send_to_client(fd, &ws_pkt);
                }
                free((void *)json_str);
            }
            cJSON_Delete(msg);
        } else {
            cJSON_Delete(diff);
        }
    }

    return current_full;
}

void websocket_api_on_connect(int fd)
{
    if (GLOBAL_STATE == NULL) {
        ESP_LOGW(TAG, "Cannot send initial state, GLOBAL_STATE not yet initialized");
        return;
    }

    // On connect, we diff against NULL to send the full current state
    cJSON *full = process_and_send_update(NULL, fd);
    cJSON_Delete(full);
}

void websocket_api_task(void *pvParameters)
{
    GLOBAL_STATE = (GlobalState *)pvParameters;
    ESP_LOGI(TAG, "websocket_api_task starting");

    // Wait until network is connected before proceeding
    while (!GLOBAL_STATE->SYSTEM_MODULE.is_connected) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Initialize the baseline state
    cJSON *last_full_json = system_api_get_full_json(GLOBAL_STATE);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(WEBSOCKET_API_RATE_LIMIT_MS));

        // If no clients are connected, hibernate to save CPU/Memory churn
        if (websocket_get_active_client_count(WS_TYPE_API) == 0) {
            if (last_full_json) {
                cJSON_Delete(last_full_json);
                last_full_json = NULL;
            }
            continue;
        }

        // We have clients. If we were hibernating, initialize the baseline now
        if (last_full_json == NULL) {
            last_full_json = system_api_get_full_json(GLOBAL_STATE);
        }

        // Process diff and rotate state
        cJSON *new_full_json = process_and_send_update(last_full_json, -1);
        
        if (new_full_json != last_full_json) {
            cJSON_Delete(last_full_json);
            last_full_json = new_full_json;
        }
    }
}
