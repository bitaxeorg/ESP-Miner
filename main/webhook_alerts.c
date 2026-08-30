#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "global_state.h"
#include "nvs_config.h"
#include "utils.h"
#include "webhook_alerts.h"

#define WEBHOOK_ALERT_QUEUE_LENGTH 4
#define WEBHOOK_ALERT_TIMEOUT_MS 5000
#define WEBHOOK_ALERT_TASK_STACK 8192
#define WEBHOOK_ALERT_MESSAGE_MAX_LEN 384

typedef enum {
    WEBHOOK_EVENT_TEST,
    WEBHOOK_EVENT_WATCHDOG_RESET,
    WEBHOOK_EVENT_BLOCK_FOUND,
    WEBHOOK_EVENT_BEST_DIFFICULTY,
} WebhookEventType;

typedef struct {
    WebhookEventType type;
    double difficulty;
    double network_difficulty;
    bool report_result;
    bool simulated;
} WebhookAlertEvent;

static const char *TAG = "webhook_alerts";
static GlobalState *GLOBAL_STATE = NULL;
static QueueHandle_t alert_queue = NULL;
static QueueHandle_t test_result_queue = NULL;
static SemaphoreHandle_t test_mutex = NULL;

static void cleanup_alert_resources(void)
{
    if (alert_queue != NULL) {
        vQueueDelete(alert_queue);
        alert_queue = NULL;
    }
    if (test_result_queue != NULL) {
        vQueueDelete(test_result_queue);
        test_result_queue = NULL;
    }
    if (test_mutex != NULL) {
        vSemaphoreDelete(test_mutex);
        test_mutex = NULL;
    }
    GLOBAL_STATE = NULL;
}

bool WEBHOOK_ALERTS_is_valid_url(const char *url)
{
    if (url == NULL) {
        return false;
    }

    size_t len = strlen(url);
    if (len <= strlen("https://") || len > WEBHOOK_ALERT_URL_MAX_LEN) {
        return false;
    }
    if (strncmp(url, "https://", strlen("https://")) != 0) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        if (iscntrl((unsigned char) url[i]) || isspace((unsigned char) url[i])) {
            return false;
        }
    }

    const char *authority = url + strlen("https://");
    const char *authority_end = authority;
    while (*authority_end != '\0' && *authority_end != '/' && *authority_end != '?' && *authority_end != '#') {
        authority_end++;
    }
    if (authority_end == authority || strchr(url, '#') != NULL) {
        return false;
    }

    size_t authority_len = authority_end - authority;
    if (memchr(authority, '@', authority_len) != NULL) {
        return false;
    }

    const char *host_end = memchr(authority, ':', authority_len);
    if (host_end == NULL) {
        host_end = authority_end;
    }
    size_t host_len = host_end - authority;
    const char *dot = memchr(authority, '.', host_len);
    return dot != NULL && dot != authority && dot + 1 < host_end;
}

bool WEBHOOK_ALERTS_has_webhook(void)
{
    char *url = nvs_config_get_string(NVS_CONFIG_WEBHOOK_URL);
    bool configured = url != NULL && WEBHOOK_ALERTS_is_valid_url(url);
    free(url);
    return configured;
}

static bool event_enabled(WebhookEventType type)
{
    switch (type) {
        case WEBHOOK_EVENT_WATCHDOG_RESET:
            return nvs_config_get_bool(NVS_CONFIG_WEBHOOK_WATCHDOG);
        case WEBHOOK_EVENT_BLOCK_FOUND:
            return nvs_config_get_bool(NVS_CONFIG_WEBHOOK_BLOCK_FOUND);
        case WEBHOOK_EVENT_BEST_DIFFICULTY:
            return nvs_config_get_bool(NVS_CONFIG_WEBHOOK_BEST_DIFF);
        case WEBHOOK_EVENT_TEST:
            return true;
        default:
            return false;
    }
}

static bool map_test_event(WebhookAlertTestEvent test_event, WebhookEventType *event_type)
{
    switch (test_event) {
        case WEBHOOK_ALERT_TEST_GENERIC:
            *event_type = WEBHOOK_EVENT_TEST;
            return true;
        case WEBHOOK_ALERT_TEST_WATCHDOG:
            *event_type = WEBHOOK_EVENT_WATCHDOG_RESET;
            return true;
        case WEBHOOK_ALERT_TEST_BLOCK_FOUND:
            *event_type = WEBHOOK_EVENT_BLOCK_FOUND;
            return true;
        case WEBHOOK_ALERT_TEST_BEST_DIFFICULTY:
            *event_type = WEBHOOK_EVENT_BEST_DIFFICULTY;
            return true;
        default:
            return false;
    }
}

bool WEBHOOK_ALERTS_is_test_event_enabled(WebhookAlertTestEvent test_event)
{
    WebhookEventType event_type;
    return map_test_event(test_event, &event_type) && event_enabled(event_type);
}

static bool is_watchdog_reset(esp_reset_reason_t reason)
{
    return reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT;
}

static void build_message(const WebhookAlertEvent *event, char *message, size_t message_len)
{
    char *hostname = nvs_config_get_string(NVS_CONFIG_HOSTNAME);
    const char *safe_hostname = hostname && hostname[0] != '\0' ? hostname : "Bitaxe";
    const char *safe_ip = "unknown";
    if (GLOBAL_STATE != NULL && GLOBAL_STATE->SYSTEM_MODULE.ip_addr_str[0] != '\0') {
        safe_ip = GLOBAL_STATE->SYSTEM_MODULE.ip_addr_str;
    }

    uint8_t mac[6] = {0};
    char formatted_mac[18] = "unknown";
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        snprintf(formatted_mac, sizeof(formatted_mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    char difficulty[32] = {0};
    char network_difficulty[32] = {0};
    if (event->difficulty > 0) {
        suffixString((uint64_t) event->difficulty, difficulty, sizeof(difficulty), 0);
    }
    if (event->network_difficulty > 0) {
        suffixString((uint64_t) event->network_difficulty, network_difficulty, sizeof(network_difficulty), 0);
    }

    char event_message[224] = {0};
    switch (event->type) {
        case WEBHOOK_EVENT_TEST:
            snprintf(event_message, sizeof(event_message), "This is a test message!");
            break;
        case WEBHOOK_EVENT_WATCHDOG_RESET:
            snprintf(event_message, sizeof(event_message), "Device rebooted after a firmware watchdog reset!");
            break;
        case WEBHOOK_EVENT_BLOCK_FOUND:
            snprintf(event_message, sizeof(event_message),
                     ":tada: Block found!\nDiff: %s (network: %s)",
                     difficulty, network_difficulty);
            break;
        case WEBHOOK_EVENT_BEST_DIFFICULTY:
            snprintf(event_message, sizeof(event_message),
                     ":chart_with_upwards_trend: New *best difficulty* found!\n"
                     "Diff: %s (network: %s)",
                     difficulty, network_difficulty);
            break;
        default:
            snprintf(event_message, sizeof(event_message), "Miner alert.");
            break;
    }

    snprintf(message, message_len, "%s%s\n```\nHostname: %s\nIP:       %s\nMAC:      %s\n```",
             event->simulated ? "[TEST] " : "", event_message, safe_hostname, safe_ip, formatted_mac);

    free(hostname);
}

static esp_err_t deliver_event(const WebhookAlertEvent *event)
{
    char *url = nvs_config_get_string(NVS_CONFIG_WEBHOOK_URL);
    if (!WEBHOOK_ALERTS_is_valid_url(url)) {
        free(url);
        return ESP_ERR_INVALID_ARG;
    }

    char message[WEBHOOK_ALERT_MESSAGE_MAX_LEN];
    build_message(event, message, sizeof(message));

    cJSON *root = cJSON_CreateObject();
    if (root == NULL || cJSON_AddStringToObject(root, "content", message) == NULL) {
        cJSON_Delete(root);
        free(url);
        return ESP_ERR_NO_MEM;
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload == NULL) {
        free(url);
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = WEBHOOK_ALERT_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(payload);
        free(url);
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "User-Agent", "ESP-Miner webhook alerts");
    esp_http_client_set_post_field(client, payload, strlen(payload));

    esp_err_t result = esp_http_client_perform(client);
    if (result == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status < 200 || status >= 300) {
            ESP_LOGW(TAG, "Webhook delivery returned HTTP %d", status);
            result = ESP_FAIL;
        }
    } else {
        ESP_LOGW(TAG, "Webhook delivery failed: %s", esp_err_to_name(result));
    }

    esp_http_client_cleanup(client);
    free(payload);
    free(url);
    return result;
}

static void webhook_alert_task(void *parameter)
{
    (void) parameter;
    WebhookAlertEvent event;
    while (true) {
        if (xQueueReceive(alert_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        esp_err_t result = deliver_event(&event);
        if (event.report_result && test_result_queue != NULL) {
            xQueueOverwrite(test_result_queue, &result);
        }
    }
}

static esp_err_t enqueue_event(WebhookEventType type, double difficulty, double network_difficulty,
                               bool report_result, bool simulated)
{
    if (alert_queue == NULL || !WEBHOOK_ALERTS_has_webhook() || !event_enabled(type)) {
        return ESP_ERR_INVALID_STATE;
    }

    WebhookAlertEvent event = {
        .type = type,
        .difficulty = difficulty,
        .network_difficulty = network_difficulty,
        .report_result = report_result,
        .simulated = simulated,
    };

    if (xQueueSend(alert_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Webhook alert queue is full; dropping event type %d", type);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t WEBHOOK_ALERTS_init(GlobalState *global_state)
{
    if (alert_queue != NULL) {
        return ESP_OK;
    }

    GLOBAL_STATE = global_state;
    alert_queue = xQueueCreate(WEBHOOK_ALERT_QUEUE_LENGTH, sizeof(WebhookAlertEvent));
    test_result_queue = xQueueCreate(1, sizeof(esp_err_t));
    test_mutex = xSemaphoreCreateMutex();
    if (alert_queue == NULL || test_result_queue == NULL || test_mutex == NULL) {
        cleanup_alert_resources();
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreateWithCaps(webhook_alert_task, "webhook_alert", WEBHOOK_ALERT_TASK_STACK, NULL, 2, NULL,
                            MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create webhook alert task");
        cleanup_alert_resources();
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void WEBHOOK_ALERTS_notify_startup(void)
{
    if (is_watchdog_reset(esp_reset_reason())) {
        enqueue_event(WEBHOOK_EVENT_WATCHDOG_RESET, 0, 0, false, false);
    }
}

void WEBHOOK_ALERTS_notify_block_found(double difficulty, double network_difficulty)
{
    enqueue_event(WEBHOOK_EVENT_BLOCK_FOUND, difficulty, network_difficulty, false, false);
}

void WEBHOOK_ALERTS_notify_best_difficulty(double difficulty, double network_difficulty)
{
    enqueue_event(WEBHOOK_EVENT_BEST_DIFFICULTY, difficulty, network_difficulty, false, false);
}

esp_err_t WEBHOOK_ALERTS_send_test(TickType_t timeout_ticks)
{
    return WEBHOOK_ALERTS_send_test_event(WEBHOOK_ALERT_TEST_GENERIC, timeout_ticks);
}

esp_err_t WEBHOOK_ALERTS_send_test_event(WebhookAlertTestEvent test_event, TickType_t timeout_ticks)
{
    WebhookEventType event_type;
    if (!map_test_event(test_event, &event_type)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (test_mutex == NULL || xSemaphoreTake(test_mutex, 0) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t stale_result;
    xQueueReceive(test_result_queue, &stale_result, 0);

    double network_difficulty = 0;
    double difficulty = 0;
    if (GLOBAL_STATE != NULL) {
        network_difficulty = (double) GLOBAL_STATE->network_nonce_diff;
        if (test_event == WEBHOOK_ALERT_TEST_BLOCK_FOUND) {
            difficulty = network_difficulty;
        } else if (test_event == WEBHOOK_ALERT_TEST_BEST_DIFFICULTY) {
            difficulty = (double) GLOBAL_STATE->SYSTEM_MODULE.best_nonce_diff + 1;
        }
    }
    if (network_difficulty <= 0 && test_event != WEBHOOK_ALERT_TEST_GENERIC &&
        test_event != WEBHOOK_ALERT_TEST_WATCHDOG) {
        network_difficulty = 1;
    }
    if (difficulty <= 0 && test_event != WEBHOOK_ALERT_TEST_GENERIC &&
        test_event != WEBHOOK_ALERT_TEST_WATCHDOG) {
        difficulty = 1;
    }

    esp_err_t result = enqueue_event(event_type, difficulty, network_difficulty, true,
                                     test_event != WEBHOOK_ALERT_TEST_GENERIC);
    if (result == ESP_OK && xQueueReceive(test_result_queue, &result, timeout_ticks) != pdTRUE) {
        result = ESP_ERR_TIMEOUT;
    }

    xSemaphoreGive(test_mutex);
    return result;
}
