#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#include "http_server.h"
#include "nvs_config.h"
#include "webhook_alert_api.h"
#include "webhook_alert_utils.h"
#include "webhook_alerts.h"

#define WEBHOOK_ALERT_REQUEST_MAX_LEN 1024
#define WEBHOOK_ALERT_REQUEST_DEADLINE_MS 10000
#define WEBHOOK_ALERT_TEST_TIMEOUT_MS 6000

static int response_prebuffer_len = 160;

static esp_err_t send_settings_values(httpd_req_t *req, bool has_webhook, bool watchdog_enabled,
                                      bool block_found_enabled, bool best_diff_enabled)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }

    cJSON_AddBoolToObject(root, "hasWebhook", has_webhook);
    cJSON_AddBoolToObject(root, "watchdogEnabled", watchdog_enabled);
    cJSON_AddBoolToObject(root, "blockFoundEnabled", block_found_enabled);
    cJSON_AddBoolToObject(root, "bestDiffEnabled", best_diff_enabled);

    httpd_resp_set_type(req, "application/json");
    set_cors_headers(req);
    esp_err_t result = HTTP_send_json(req, root, &response_prebuffer_len);
    cJSON_Delete(root);
    return result;
}

static esp_err_t send_settings(httpd_req_t *req)
{
    return send_settings_values(req,
                                WEBHOOK_ALERTS_has_webhook(),
                                nvs_config_get_bool(NVS_CONFIG_WEBHOOK_WATCHDOG),
                                nvs_config_get_bool(NVS_CONFIG_WEBHOOK_BLOCK_FOUND),
                                nvs_config_get_bool(NVS_CONFIG_WEBHOOK_BEST_DIFF));
}

static esp_err_t GET_webhook_alert_settings(httpd_req_t *req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }
    return send_settings(req);
}

static esp_err_t receive_json(httpd_req_t *req, cJSON **root)
{
    if (req->content_len <= 0 || req->content_len >= WEBHOOK_ALERT_REQUEST_MAX_LEN) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request size");
    }

    char *body = malloc(req->content_len + 1);
    if (body == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }

    int received_total = 0;
    int64_t request_start_us = esp_timer_get_time();
    while (received_total < req->content_len) {
        if (WEBHOOK_ALERT_UTILS_deadline_expired(request_start_us, esp_timer_get_time(),
                                                 WEBHOOK_ALERT_REQUEST_DEADLINE_MS)) {
            free(body);
            httpd_resp_set_status(req, "408 Request Timeout");
            return httpd_resp_send(req, "Request body timed out", HTTPD_RESP_USE_STRLEN);
        }

        int received = httpd_req_recv(req, body + received_total, req->content_len - received_total);
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            if (WEBHOOK_ALERT_UTILS_deadline_expired(request_start_us, esp_timer_get_time(),
                                                     WEBHOOK_ALERT_REQUEST_DEADLINE_MS)) {
                free(body);
                httpd_resp_set_status(req, "408 Request Timeout");
                return httpd_resp_send(req, "Request body timed out", HTTPD_RESP_USE_STRLEN);
            }
            continue;
        }
        if (received <= 0) {
            free(body);
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
        }
        received_total += received;
        if (WEBHOOK_ALERT_UTILS_deadline_expired(request_start_us, esp_timer_get_time(),
                                                 WEBHOOK_ALERT_REQUEST_DEADLINE_MS)) {
            free(body);
            httpd_resp_set_status(req, "408 Request Timeout");
            return httpd_resp_send(req, "Request body timed out", HTTPD_RESP_USE_STRLEN);
        }
    }
    body[received_total] = '\0';

    *root = cJSON_Parse(body);
    free(body);
    if (*root == NULL || !cJSON_IsObject(*root)) {
        cJSON_Delete(*root);
        *root = NULL;
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }
    return ESP_OK;
}

static bool validate_optional_bool(const cJSON *root, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    return item == NULL || cJSON_IsBool(item);
}

static esp_err_t PATCH_webhook_alert_settings(httpd_req_t *req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    cJSON *root = NULL;
    esp_err_t result = receive_json(req, &root);
    if (result != ESP_OK) {
        return result;
    }

    cJSON *url = cJSON_GetObjectItemCaseSensitive(root, "webhookUrl");
    if (url != NULL && !cJSON_IsString(url)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "webhookUrl must be a string");
    }
    if (cJSON_IsString(url) && url->valuestring[0] != '\0' &&
        strcmp(url->valuestring, WEBHOOK_ALERT_SECRET_SENTINEL) != 0 &&
        !WEBHOOK_ALERTS_is_valid_url(url->valuestring)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "webhookUrl must be a valid HTTPS URL");
    }
    if (!validate_optional_bool(root, "watchdogEnabled") ||
        !validate_optional_bool(root, "blockFoundEnabled") ||
        !validate_optional_bool(root, "bestDiffEnabled")) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Alert switches must be booleans");
    }

    bool has_webhook = WEBHOOK_ALERTS_has_webhook();
    if (cJSON_IsString(url) && strcmp(url->valuestring, WEBHOOK_ALERT_SECRET_SENTINEL) != 0) {
        has_webhook = url->valuestring[0] != '\0';
        nvs_config_set_string(NVS_CONFIG_WEBHOOK_URL, url->valuestring);
    }

    bool watchdog_enabled = nvs_config_get_bool(NVS_CONFIG_WEBHOOK_WATCHDOG);
    bool block_found_enabled = nvs_config_get_bool(NVS_CONFIG_WEBHOOK_BLOCK_FOUND);
    bool best_diff_enabled = nvs_config_get_bool(NVS_CONFIG_WEBHOOK_BEST_DIFF);

    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "watchdogEnabled");
    if (item != NULL) {
        watchdog_enabled = cJSON_IsTrue(item);
        nvs_config_set_bool(NVS_CONFIG_WEBHOOK_WATCHDOG, watchdog_enabled);
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "blockFoundEnabled");
    if (item != NULL) {
        block_found_enabled = cJSON_IsTrue(item);
        nvs_config_set_bool(NVS_CONFIG_WEBHOOK_BLOCK_FOUND, block_found_enabled);
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "bestDiffEnabled");
    if (item != NULL) {
        best_diff_enabled = cJSON_IsTrue(item);
        nvs_config_set_bool(NVS_CONFIG_WEBHOOK_BEST_DIFF, best_diff_enabled);
    }

    cJSON_Delete(root);
    // NVS writes are applied by a background task. Echo the accepted values so
    // the UI can immediately show the masked secret and enable Test/Clear.
    return send_settings_values(req, has_webhook, watchdog_enabled, block_found_enabled, best_diff_enabled);
}

static esp_err_t parse_test_event(httpd_req_t *req, WebhookAlertTestEvent *test_event)
{
    *test_event = WEBHOOK_ALERT_TEST_GENERIC;
    if (req->content_len == 0) {
        return ESP_OK;
    }

    cJSON *root = NULL;
    esp_err_t result = receive_json(req, &root);
    if (result != ESP_OK) {
        return result;
    }

    const cJSON *event = cJSON_GetObjectItemCaseSensitive(root, "event");
    if (event == NULL) {
        cJSON_Delete(root);
        return ESP_OK;
    }
    if (!cJSON_IsString(event)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "event must be a string");
    }

    if (strcmp(event->valuestring, "watchdog") == 0) {
        *test_event = WEBHOOK_ALERT_TEST_WATCHDOG;
    } else if (strcmp(event->valuestring, "block-found") == 0) {
        *test_event = WEBHOOK_ALERT_TEST_BLOCK_FOUND;
    } else if (strcmp(event->valuestring, "best-diff") == 0) {
        *test_event = WEBHOOK_ALERT_TEST_BEST_DIFFICULTY;
    } else if (strcmp(event->valuestring, "generic") != 0) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown webhook test event");
    }

    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t POST_webhook_alert_test(httpd_req_t *req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }
    if (!WEBHOOK_ALERTS_has_webhook()) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No webhook is configured");
    }

    WebhookAlertTestEvent test_event;
    esp_err_t result = parse_test_event(req, &test_event);
    if (result != ESP_OK) {
        return result;
    }
    if (!WEBHOOK_ALERTS_is_test_event_enabled(test_event)) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_send(req, "Requested webhook alert is disabled", HTTPD_RESP_USE_STRLEN);
    }

    result = WEBHOOK_ALERTS_send_test_event(test_event, pdMS_TO_TICKS(WEBHOOK_ALERT_TEST_TIMEOUT_MS));
    if (result == ESP_ERR_INVALID_STATE) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_send(req, "Another webhook test is already running", HTTPD_RESP_USE_STRLEN);
    }
    if (result != ESP_OK) {
        httpd_resp_set_status(req, "502 Bad Gateway");
        return httpd_resp_send(req, "Webhook delivery failed", HTTPD_RESP_USE_STRLEN);
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    cJSON_AddStringToObject(root, "message", "Webhook test delivered");
    httpd_resp_set_type(req, "application/json");
    set_cors_headers(req);
    result = HTTP_send_json(req, root, &response_prebuffer_len);
    cJSON_Delete(root);
    return result;
}

esp_err_t register_webhook_alert_api(httpd_handle_t server)
{
    const httpd_uri_t get_settings = {
        .uri = "/api/system/alerts",
        .method = HTTP_GET,
        .handler = GET_webhook_alert_settings,
    };
    const httpd_uri_t patch_settings = {
        .uri = "/api/system/alerts",
        .method = HTTP_PATCH,
        .handler = PATCH_webhook_alert_settings,
    };
    const httpd_uri_t test_alert = {
        .uri = "/api/system/alerts/test",
        .method = HTTP_POST,
        .handler = POST_webhook_alert_test,
    };

    esp_err_t result = httpd_register_uri_handler(server, &get_settings);
    if (result == ESP_OK) {
        result = httpd_register_uri_handler(server, &patch_settings);
    }
    if (result == ESP_OK) {
        result = httpd_register_uri_handler(server, &test_alert);
    }
    return result;
}
