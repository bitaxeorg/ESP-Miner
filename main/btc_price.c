#include "btc_price.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "BTC_PRICE";

// 使用 HTTP（不加密，避免 TLS 问题）
#define BTC_PRICE_URL "http://data-api.binance.vision/api/v3/ticker/price?symbol=BTCUSDT"
#define UPDATE_INTERVAL_MS (60 * 1000)
#define MAX_PRICE_STR_LEN 16

static char g_current_price[MAX_PRICE_STR_LEN] = "N/A";
static bool g_price_available = false;
static SemaphoreHandle_t g_price_mutex = NULL;

static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    static char *output_buffer = NULL;
    static size_t output_len = 0;

    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA: {
            if (output_buffer == NULL) {
                output_buffer = malloc(256);
                if (output_buffer == NULL) {
                    ESP_LOGE(TAG, "Memory allocation failed");
                    return ESP_ERR_NO_MEM;
                }
                output_len = 0;
                memset(output_buffer, 0, 256);
            }
            if (output_len + evt->data_len >= 256) {
                char *new_buf = realloc(output_buffer, output_len + evt->data_len + 1);
                if (new_buf == NULL) {
                    free(output_buffer);
                    output_buffer = NULL;
                    ESP_LOGE(TAG, "Memory reallocation failed");
                    return ESP_ERR_NO_MEM;
                }
                output_buffer = new_buf;
            }
            memcpy(output_buffer + output_len, evt->data, evt->data_len);
            output_len += evt->data_len;
            output_buffer[output_len] = '\0';
            break;
        }
        case HTTP_EVENT_ON_FINISH: {
            if (output_buffer != NULL && output_len > 0) {
                ESP_LOGI(TAG, "Raw response: %s", output_buffer);
                cJSON *root = cJSON_Parse(output_buffer);
                if (root != NULL) {
                    cJSON *price_item = cJSON_GetObjectItem(root, "price");
                    if (cJSON_IsString(price_item) && price_item->valuestring != NULL) {
                        if (g_price_mutex != NULL) {
                            xSemaphoreTake(g_price_mutex, portMAX_DELAY);
                            strncpy(g_current_price, price_item->valuestring, MAX_PRICE_STR_LEN - 1);
                            g_current_price[MAX_PRICE_STR_LEN - 1] = '\0';
                            g_price_available = true;
                            xSemaphoreGive(g_price_mutex);
                            ESP_LOGI(TAG, "Price updated: %s", g_current_price);
                        }
                    } else {
                        ESP_LOGW(TAG, "Price field not found");
                    }
                    cJSON_Delete(root);
                } else {
                    ESP_LOGW(TAG, "Failed to parse JSON");
                }
            }
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
                output_len = 0;
            }
            break;
        }
        case HTTP_EVENT_ERROR: {
            ESP_LOGE(TAG, "HTTP error occurred");
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
                output_len = 0;
            }
            break;
        }
        default:
            break;
    }
    return ESP_OK;
}

static void _fetch_price(void) {
    ESP_LOGI(TAG, "Fetching price from Binance...");
    
    esp_http_client_config_t config = {
        .url = BTC_PRICE_URL,
        .method = HTTP_METHOD_GET,
        .event_handler = _http_event_handler,
        .timeout_ms = 10000,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    } else {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP status: %d", status_code);
    }
    esp_http_client_cleanup(client);
}

static void _price_task(void *pvParameters) {
    ESP_LOGI(TAG, "Price update task started");
    vTaskDelay(pdMS_TO_TICKS(15000));
    
    while (1) {
        _fetch_price();
        vTaskDelay(pdMS_TO_TICKS(UPDATE_INTERVAL_MS));
    }
}

void btc_price_init(void) {
    g_price_mutex = xSemaphoreCreateMutex();
    if (g_price_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return;
    }
    
    BaseType_t task_created = xTaskCreate(
        _price_task,
        "btc_price_task",
        4096,
        NULL,
        5,
        NULL
    );
    
    if (task_created == pdPASS) {
        ESP_LOGI(TAG, "BTC price task initialized successfully");
    } else {
        ESP_LOGE(TAG, "Failed to create BTC price task");
    }
}

const char* btc_price_get_current(void) {
    if (g_price_mutex == NULL) {
        return "N/A";
    }
    
    static char return_price[MAX_PRICE_STR_LEN];
    xSemaphoreTake(g_price_mutex, portMAX_DELAY);
    strncpy(return_price, g_current_price, MAX_PRICE_STR_LEN - 1);
    return_price[MAX_PRICE_STR_LEN - 1] = '\0';
    xSemaphoreGive(g_price_mutex);
    return return_price;
}

bool btc_price_is_available(void) {
    if (g_price_mutex == NULL) {
        return false;
    }
    bool available;
    xSemaphoreTake(g_price_mutex, portMAX_DELAY);
    available = g_price_available;
    xSemaphoreGive(g_price_mutex);
    return available;
}