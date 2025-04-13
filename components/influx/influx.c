#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "influx.h"

static const char *TAG = "influx";

#define INFLUX_BUFFER_SIZE 2048
#define INFLUX_API_VERSION "2.0"
#define INFLUX_WRITE_PATH "/api/v2/write"
#define INFLUX_BUCKETS_PATH "/api/v2/buckets"

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    // Log the event ID for debugging
    ESP_LOGD(TAG, "HTTP Event: %d", evt->event_id);

    // Handle specific events
    if (evt->event_id == HTTP_EVENT_ERROR) {
        ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
    } else if (evt->event_id == HTTP_EVENT_ON_CONNECTED) {
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
    } else if (evt->event_id == HTTP_EVENT_ON_HEADER) {
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
    } else if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (!esp_http_client_is_chunked_response(evt->client)) {
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        }
    } else if (evt->event_id == HTTP_EVENT_ON_FINISH) {
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
    } else if (evt->event_id == HTTP_EVENT_DISCONNECTED) {
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
    } else if (evt->event_id == HTTP_EVENT_REDIRECT) {
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
    }

    return ESP_OK;
}

bool influx_init(influx_client_t *client, const char *host, int port,
                const char *token, const char *bucket, const char *org,
                const char *prefix)
{
    if (!client || !host || !token || !bucket || !org || !prefix) {
        ESP_LOGE(TAG, "Invalid parameters");
        return false;
    }

    memset(client, 0, sizeof(influx_client_t));

    // Allocate and copy strings
    client->host = strdup(host);
    client->token = strdup(token);
    client->bucket = strdup(bucket);
    client->org = strdup(org);
    client->prefix = strdup(prefix);
    client->port = port;

    // Initialize mutex
    if (pthread_mutex_init(&client->lock, NULL) != 0) {
        ESP_LOGE(TAG, "Mutex initialization failed");
        influx_deinit(client);
        return false;
    }

    // Allocate buffer for HTTP requests
    client->big_buffer = malloc(INFLUX_BUFFER_SIZE);
    if (!client->big_buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        influx_deinit(client);
        return false;
    }

    // Create auth header
    snprintf(client->auth_header, sizeof(client->auth_header), "Token %s", token);

    return true;
}

void influx_deinit(influx_client_t *client)
{
    if (!client) return;

    pthread_mutex_destroy(&client->lock);
    free(client->host);
    free(client->token);
    free(client->bucket);
    free(client->org);
    free(client->prefix);
    free(client->big_buffer);
    memset(client, 0, sizeof(influx_client_t));
}

bool influx_write(influx_client_t *client)
{
    if (!client) return false;

    pthread_mutex_lock(&client->lock);

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d%s?bucket=%s&org=%s&precision=s",
             client->host, client->port, INFLUX_WRITE_PATH,
             client->bucket, client->org);

    int len = snprintf(client->big_buffer, INFLUX_BUFFER_SIZE,
                      "%s asic_temp=%f,vr_temp=%f,"
                      "hashing_speed=%f,invalid_shares=%d,valid_shares=%d,uptime=%d,"
                      "best_difficulty=%f,total_best_difficulty=%f,pool_errors=%d,"
                      "accepted=%d,not_accepted=%d,total_uptime=%d,blocks_found=%d,"
                      "voltage=%f,current=%f,power=%f,"
                      "total_blocks_found=%d,duplicate_hashes=%d",
                      client->prefix,
                      (double)client->stats.asicTemp,
                      (double)client->stats.vrTemp,
                      (double)client->stats.hashingSpeed,
                      client->stats.invalidShares,
                      client->stats.validShares,
                      client->stats.uptime,
                      (double)client->stats.bestSessionDifficulty,
                      (double)client->stats.totalBestDifficulty,
                      client->stats.poolErrors,
                      client->stats.accepted,
                      client->stats.notAccepted,
                      client->stats.totalUptime,
                      client->stats.blocksFound,
                      (double)client->stats.voltage,
                      (double)client->stats.current,
                      (double)client->stats.power,
                      client->stats.totalBlocksFound,
                      client->stats.duplicateHashes);

    if (len >= INFLUX_BUFFER_SIZE) {
        ESP_LOGE(TAG, "Buffer too small for data");
        pthread_mutex_unlock(&client->lock);
        return false;
    }

    ESP_LOGI(TAG, "URL: %s", url);
    ESP_LOGI(TAG, "POST: %s", client->big_buffer);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .method = HTTP_METHOD_POST,
    };

    esp_http_client_handle_t http_client = esp_http_client_init(&config);
    if (!http_client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        pthread_mutex_unlock(&client->lock);
        return false;
    }

    esp_http_client_set_header(http_client, "Authorization", client->auth_header);
    esp_http_client_set_header(http_client, "Content-Type", "text/plain; charset=utf-8");
    esp_http_client_set_post_field(http_client, client->big_buffer, len);

    esp_err_t err = esp_http_client_perform(http_client);
    esp_http_client_cleanup(http_client);

    pthread_mutex_unlock(&client->lock);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

bool influx_bucket_exists(influx_client_t *client)
{
    if (!client) return false;

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d%s?org=%s&name=%s",
             client->host, client->port, INFLUX_BUCKETS_PATH,
             client->org, client->bucket);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .method = HTTP_METHOD_GET,
    };

    esp_http_client_handle_t http_client = esp_http_client_init(&config);
    if (!http_client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return false;
    }

    esp_http_client_set_header(http_client, "Authorization", client->auth_header);

    esp_err_t err = esp_http_client_perform(http_client);
    int status_code = esp_http_client_get_status_code(http_client);
    esp_http_client_cleanup(http_client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
        return false;
    }

    return status_code == 200;
}

bool influx_create_bucket(influx_client_t *client)
{
    if (!client) return false;

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d%s",
             client->host, client->port, INFLUX_BUCKETS_PATH);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "orgID", client->org);
    cJSON_AddStringToObject(root, "name", client->bucket);
    cJSON_AddStringToObject(root, "retentionRules", "[]");

    char *post_data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!post_data) {
        ESP_LOGE(TAG, "Failed to create JSON");
        return false;
    }

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .method = HTTP_METHOD_POST,
    };

    esp_http_client_handle_t http_client = esp_http_client_init(&config);
    if (!http_client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        free(post_data);
        return false;
    }

    esp_http_client_set_header(http_client, "Authorization", client->auth_header);
    esp_http_client_set_header(http_client, "Content-Type", "application/json");
    esp_http_client_set_post_field(http_client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(http_client);
    int status_code = esp_http_client_get_status_code(http_client);
    esp_http_client_cleanup(http_client);
    free(post_data);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
        return false;
    }

    return status_code == 201;
}