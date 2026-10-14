#include <string.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "websocket_api.h"
#include "websocket.h"
#include "global_state.h"
#include "system_api_json.h"
#include "yyjson.h"
#include "yyjson_utils.h"

#define WEBSOCKET_API_RATE_LIMIT_MS 500

static const char *TAG = "websocket_api";
static GlobalState *GLOBAL_STATE = NULL;

/**
 * @brief Builds the JSON update message and broadcasts it.
 * 
 * @param last_state The previous full JSON state (will be used for diffing).
 * @param fd Client file descriptor (-1 for broadcast).
 * @return yyjson_mut_doc* The new full JSON state that should track current reality.
 */
static yyjson_mut_doc *process_and_send_update(yyjson_mut_doc *last_state, int fd)
{
    const yyjson_alc *alc = yyjson_psram_alc();
    yyjson_mut_doc *current_full = system_api_get_full_doc(GLOBAL_STATE, alc);
    if (!current_full) return last_state;

    yyjson_mut_doc *msg_doc = yyjson_mut_doc_new(alc);
    if (!msg_doc) {
        return current_full;
    }

    yyjson_mut_val *old_root = last_state ? yyjson_mut_doc_get_root(last_state) : NULL;
    yyjson_mut_val *new_root = yyjson_mut_doc_get_root(current_full);
    yyjson_mut_val *diff = yyjson_diff_objects(msg_doc, old_root, new_root);

    if (diff == NULL) {
        yyjson_mut_doc_free(msg_doc);
        if (last_state) {
            yyjson_mut_doc_free(current_full);
            return last_state;
        }
        return current_full;
    }

    yyjson_mut_val *msg = yyjson_mut_obj(msg_doc);
    if (msg) {
        yyjson_mut_doc_set_root(msg_doc, msg);
        yyjson_mut_obj_add_str(msg_doc, msg, "event", "update");
        yyjson_mut_obj_add_val(msg_doc, msg, "data", diff);

        size_t len = 0;
        char *json_str = yyjson_mut_write_opts(msg_doc, YYJSON_WRITE_INF_AND_NAN_AS_NULL | YYJSON_WRITE_ALLOW_INVALID_UNICODE, alc, &len, NULL);
        if (json_str != NULL) {
            httpd_ws_frame_t ws_pkt;
            memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
            ws_pkt.payload = (uint8_t *)json_str;
            ws_pkt.len = len;
            ws_pkt.type = HTTPD_WS_TYPE_TEXT;

            if (fd == -1) {
                websocket_broadcast(WS_TYPE_API, &ws_pkt);
            } else {
                websocket_send_to_client(fd, &ws_pkt);
            }
            yyjson_alc_free(alc, json_str);
        }
    }

    yyjson_mut_doc_free(msg_doc);
    return current_full;
}

void websocket_api_on_connect(int fd)
{
    if (GLOBAL_STATE == NULL) {
        ESP_LOGW(TAG, "Cannot send initial state, GLOBAL_STATE not yet initialized");
        return;
    }

    // On connect, we diff against NULL to send the full current state
    yyjson_mut_doc *full = process_and_send_update(NULL, fd);
    if (full) {
        yyjson_mut_doc_free(full);
    }
}

void websocket_api_task(void *pvParameters)
{
    GLOBAL_STATE = (GlobalState *)pvParameters;
    ESP_LOGI(TAG, "websocket_api_task starting");

    // Wait until network is connected before proceeding
    while (!GLOBAL_STATE->SYSTEM_MODULE.is_connected) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    const yyjson_alc *alc = yyjson_psram_alc();
    // Initialize the baseline state
    yyjson_mut_doc *last_full_doc = system_api_get_full_doc(GLOBAL_STATE, alc);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(WEBSOCKET_API_RATE_LIMIT_MS));

        // If no clients are connected, hibernate to save CPU/Memory churn
        if (websocket_get_active_client_count(WS_TYPE_API) == 0) {
            if (last_full_doc) {
                yyjson_mut_doc_free(last_full_doc);
                last_full_doc = NULL;
            }
            continue;
        }

        // We have clients. If we were hibernating, initialize the baseline now
        if (last_full_doc == NULL) {
            last_full_doc = system_api_get_full_doc(GLOBAL_STATE, alc);
        }

        // Process diff and rotate state
        yyjson_mut_doc *new_full_doc = process_and_send_update(last_full_doc, -1);
        
        if (new_full_doc != last_full_doc) {
            if (last_full_doc) {
                yyjson_mut_doc_free(last_full_doc);
            }
            last_full_doc = new_full_doc;
        }
    }
}
