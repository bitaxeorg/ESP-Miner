#include "webhook_task.h"
#include "global_state.h"
#include "nvs_config.h"
#include "connect.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "system.h"
#include <string.h>
#include <sys/time.h>

static const char *TAG = "webhook_task";

// HTTP event handler
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }
    return ESP_OK;
}

// Send webhook POST request
static esp_err_t send_webhook(GlobalState *GLOBAL_STATE, const char *url, const char *json_data)
{
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_data, strlen(json_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        int content_length = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG, "Webhook sent successfully, status=%d, content_length=%d", status_code, content_length);
    } else {
        ESP_LOGE(TAG, "Webhook send failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

// Check if WiFi is connected
static bool is_wifi_connected()
{
    wifi_ap_record_t ap_info;
    return (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
}

void webhook_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;

    ESP_LOGI(TAG, "Webhook task started");

    while (1) {
        // Check if webhook is enabled
        bool webhook_enabled = nvs_config_get_bool(NVS_CONFIG_WEBHOOK_ENABLED);
        
        if (!webhook_enabled) {
            vTaskDelay(10000 / portTICK_PERIOD_MS); // Check every 10 seconds if disabled
            continue;
        }

        // Get webhook URL and interval
        char *webhook_url = nvs_config_get_string(NVS_CONFIG_WEBHOOK_URL);
        uint16_t interval = nvs_config_get_u16(NVS_CONFIG_WEBHOOK_INTERVAL);

        // Validate URL
        if (webhook_url == NULL || strlen(webhook_url) == 0) {
            ESP_LOGW(TAG, "Webhook enabled but URL is empty");
            free(webhook_url);
            vTaskDelay(60000 / portTICK_PERIOD_MS); // Wait 1 minute before retry
            continue;
        }

        // Check WiFi connection
        if (!is_wifi_connected()) {
            ESP_LOGD(TAG, "WiFi not connected, skipping webhook");
            free(webhook_url);
            vTaskDelay(10000 / portTICK_PERIOD_MS); // Wait 10 seconds
            continue;
        }

        // Create system info JSON
        cJSON *json = SYSTEM_create_info_json(GLOBAL_STATE);
        if (json == NULL) {
            ESP_LOGE(TAG, "Failed to create system info JSON");
            free(webhook_url);
            vTaskDelay(interval * 1000 / portTICK_PERIOD_MS);
            continue;
        }

        // Convert JSON to string
        char *json_string = cJSON_Print(json);
        if (json_string == NULL) {
            ESP_LOGE(TAG, "Failed to convert JSON to string");
            cJSON_Delete(json);
            free(webhook_url);
            vTaskDelay(interval * 1000 / portTICK_PERIOD_MS);
            continue;
        }

        // Send webhook
        ESP_LOGI(TAG, "Sending webhook to %s", webhook_url);
        send_webhook(GLOBAL_STATE, webhook_url, json_string);

        // Cleanup
        free(json_string);
        cJSON_Delete(json);
        free(webhook_url);

        // Wait for next interval
        vTaskDelay(interval * 1000 / portTICK_PERIOD_MS);
    }
}
