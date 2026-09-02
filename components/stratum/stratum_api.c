/******************************************************************************
 *  *
 * References:
 *  1. Stratum Protocol - [link](https://reference.cash/mining/stratum-protocol)
 *****************************************************************************/

#include "stratum_api.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_transport.h"
#include "esp_transport_ssl.h"
#include "esp_transport_tcp.h"
#include "esp_crt_bundle.h"
#include "utils.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/param.h>

#define TRANSPORT_TIMEOUT_MS 5000
#define BUFFER_SIZE 1024
#define MAX_EXTRANONCE_2_LEN 32
#define JSON_RPC_BUFFER_LIMIT (STRATUM_V1_MAX_JSON_LINE_SIZE + 2U)
#define MAX_JOB_ID_LENGTH 256U
#define MAX_EXTRANONCE_1_BYTES 32U
#define MAX_POOL_DIFFICULTY 1.0e18
static const char * TAG = "stratum_api";

static char * json_rpc_buffer = NULL;
static size_t json_rpc_buffer_size = 0;
static size_t json_rpc_buffer_len = 0;

static RequestTiming *request_timings = NULL;

static RequestTiming* get_request_timing(int request_id) {
    if (request_id < 0 || request_timings == NULL) return NULL;
    int index = request_id % MAX_REQUEST_IDS;
    return &request_timings[index];
}

float STRATUM_V1_get_response_time_ms(int request_id, int64_t receive_time_us)
{
    if (request_id < 0) return -1.0;
    
    RequestTiming *timing = get_request_timing(request_id);
    if (!timing || !timing->tracking) {
        return -1.0;
    }
    
    float response_time = (receive_time_us - timing->timestamp_us) / 1000.0f;
    timing->tracking = false;
    return response_time;
}

esp_transport_handle_t STRATUM_V1_transport_init(tls_mode tls, char * cert)
{
    esp_transport_handle_t transport;
    // tls_transport
    if (tls == DISABLED)
    {
        // tcp_transport
        ESP_LOGI(TAG, "TLS disabled, Using TCP transport");
        transport = esp_transport_tcp_init();
    }
    else{
        // tls_transport
        ESP_LOGI(TAG, "Using TLS transport");
        transport = esp_transport_ssl_init();
        if (transport == NULL) {
            ESP_LOGE(TAG, "Failed to initialize SSL transport");
            return NULL;
        }
        switch(tls){
            case BUNDLED_CRT:
                ESP_LOGI(TAG, "Using default cert bundle");
                esp_transport_ssl_crt_bundle_attach(transport, esp_crt_bundle_attach);
                break;
            case CUSTOM_CRT:
                ESP_LOGI(TAG, "Using custom cert");
                if (cert == NULL) {
                    ESP_LOGE(TAG, "Error: no TLS certificate");
                    return NULL;
                }
                esp_transport_ssl_set_cert_data(transport, cert, strlen(cert));
                break;
            default:
                ESP_LOGE(TAG, "Invalid TLS mode");
                esp_transport_destroy(transport);
                return NULL;
        }
    }
    return transport;
}

bool STRATUM_V1_initialize_buffer(void)
{
    // Free any existing buffer (may be non-NULL if a previous V1 task was running)
    free(json_rpc_buffer);

    json_rpc_buffer = malloc(BUFFER_SIZE);
    json_rpc_buffer_size = BUFFER_SIZE;
    if (json_rpc_buffer == NULL) {
        json_rpc_buffer_size = 0;
        ESP_LOGE(TAG, "Failed to allocate JSON-RPC receive buffer");
        return false;
    }
    json_rpc_buffer_len = 0;
    json_rpc_buffer[0] = '\0';

    if (request_timings == NULL) {
        size_t timings_size = sizeof(RequestTiming) * MAX_REQUEST_IDS;
#ifdef CONFIG_SPIRAM
        if (esp_psram_is_initialized()) {
            request_timings = heap_caps_malloc(timings_size,
                                               MALLOC_CAP_SPIRAM);
        }
#endif
        if (request_timings == NULL) {
            request_timings = malloc(timings_size);
        }
        if (request_timings == NULL) {
            ESP_LOGE(TAG, "Failed to allocate Stratum request timings");
            free(json_rpc_buffer);
            json_rpc_buffer = NULL;
            json_rpc_buffer_size = 0;
            return false;
        }
    }

    for (int i = 0; i < MAX_REQUEST_IDS; i++) {
        request_timings[i].timestamp_us = 0;
        request_timings[i].tracking = false;
    }

    return true;
}

void cleanup_stratum_buffer()
{
    free(json_rpc_buffer);
    json_rpc_buffer = NULL;
    json_rpc_buffer_size = 0;
    json_rpc_buffer_len = 0;
    if (request_timings) {
        free(request_timings);
        request_timings = NULL;
    }
}

static bool ensure_json_buffer_capacity(size_t required_size)
{
    if (required_size > JSON_RPC_BUFFER_LIMIT) {
        return false;
    }

    if (required_size <= json_rpc_buffer_size) {
        return true;
    }

    size_t new_size = json_rpc_buffer_size;
    while (new_size < required_size && new_size < JSON_RPC_BUFFER_LIMIT) {
        new_size = MIN(new_size + BUFFER_SIZE, JSON_RPC_BUFFER_LIMIT);
    }

    char *new_buffer = realloc(json_rpc_buffer, new_size);
    if (new_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to grow JSON-RPC receive buffer to %zu bytes", new_size);
        return false;
    }

    json_rpc_buffer = new_buffer;
    json_rpc_buffer_size = new_size;
    return true;
}

char * STRATUM_V1_receive_jsonrpc_line(esp_transport_handle_t transport)
{
    if (json_rpc_buffer == NULL) {
        if (!STRATUM_V1_initialize_buffer()) {
            return NULL;
        }
    }
    char *line = NULL;
    char recv_buffer[BUFFER_SIZE];
    int nbytes;

    char *newline_pos = memchr(json_rpc_buffer, '\n', json_rpc_buffer_len);
    while (newline_pos == NULL) {
        size_t receive_capacity =
            (STRATUM_V1_MAX_JSON_LINE_SIZE + 1U) - json_rpc_buffer_len;
        size_t receive_size = MIN(sizeof(recv_buffer), receive_capacity);
        nbytes = esp_transport_read(transport, recv_buffer, receive_size,
                                    TRANSPORT_TIMEOUT_MS);
        if (nbytes < 0) {
            const char *err_str;
            switch(nbytes) {
                case ERR_TCP_TRANSPORT_NO_MEM:
                    err_str = "No memory available";
                    break;
                case ERR_TCP_TRANSPORT_CONNECTION_FAILED:
                    err_str = "Connection failed";
                    break;
                case ERR_TCP_TRANSPORT_CONNECTION_CLOSED_BY_FIN:
                    err_str = "Connection closed by peer";
                    break;
                default:
                    err_str = "Unknown error";
                    break;
            }
            ESP_LOGE(TAG, "Error: transport read failed: %s (code: %d)", err_str, nbytes);
            json_rpc_buffer_len = 0;
            json_rpc_buffer[0] = '\0';
            return NULL;
        }
        if (nbytes > 0) {
            if (memchr(recv_buffer, '\0', (size_t)nbytes) != NULL) {
                ESP_LOGE(TAG, "JSON-RPC stream contains an embedded NUL byte");
                json_rpc_buffer_len = 0;
                json_rpc_buffer[0] = '\0';
                return NULL;
            }

            size_t required_size = json_rpc_buffer_len + (size_t)nbytes + 1U;
            if (!ensure_json_buffer_capacity(required_size)) {
                json_rpc_buffer_len = 0;
                json_rpc_buffer[0] = '\0';
                return NULL;
            }

            memcpy(json_rpc_buffer + json_rpc_buffer_len, recv_buffer,
                   (size_t)nbytes);
            json_rpc_buffer_len += (size_t)nbytes;
            json_rpc_buffer[json_rpc_buffer_len] = '\0';
            newline_pos = memchr(json_rpc_buffer, '\n', json_rpc_buffer_len);

            if (newline_pos == NULL &&
                json_rpc_buffer_len > STRATUM_V1_MAX_JSON_LINE_SIZE) {
                ESP_LOGE(TAG, "JSON-RPC line exceeds %u bytes",
                         STRATUM_V1_MAX_JSON_LINE_SIZE);
                json_rpc_buffer_len = 0;
                json_rpc_buffer[0] = '\0';
                return NULL;
            }
        }
    }

    // Extract the line
    if (newline_pos) {
        size_t line_len = (size_t)(newline_pos - json_rpc_buffer);
        line = strndup(json_rpc_buffer, line_len);  // Copy only up to \n
        size_t remaining_len = json_rpc_buffer_len - line_len - 1U;
        if (remaining_len > 0) {
            memmove(json_rpc_buffer, newline_pos + 1, remaining_len);
        }
        json_rpc_buffer_len = remaining_len;
        json_rpc_buffer[json_rpc_buffer_len] = '\0';
    }
    return line;
}

void STRATUM_V1_reset_message(StratumApiV1Message *message)
{
    if (message->error_str) {
        free(message->error_str);
        message->error_str = NULL;
    }
    if (message->extranonce_str) {
        free(message->extranonce_str);
        message->extranonce_str = NULL;
    }
    if (message->show_message) {
        free(message->show_message);
        message->show_message = NULL;
    }
    if (message->version_string) {
        free(message->version_string);
        message->version_string = NULL;
    }
    if (message->mining_notification) {
        // mining_notification is usually handled by ownership transfer in stratum_task.c
        // but if it wasn't enqueued, we must free it here to avoid leaks.
        // In most cases where it *is* enqueued, the caller should have NULLed the pointer
        // after enqueuing.
        STRATUM_V1_free_mining_notify(message->mining_notification);
        message->mining_notification = NULL;
    }
    message->method = METHOD_UNKNOWN;
    message->message_id = -1;
    message->response_success = false;
    message->new_difficulty = 0.0;
    message->version_mask = 0;
}

static stratum_method parse_method(const cJSON *method_json)
{
    if (!method_json || !cJSON_IsString(method_json)) {
        return STRATUM_RESULT;
    }

    const char *method = method_json->valuestring;
    if (strcmp(method, "mining.notify") == 0) return MINING_NOTIFY;
    if (strcmp(method, "mining.set_difficulty") == 0) return MINING_SET_DIFFICULTY;
    if (strcmp(method, "mining.set_extranonce") == 0) return MINING_SET_EXTRANONCE;
    if (strcmp(method, "mining.set_version_mask") == 0) return MINING_SET_VERSION_MASK;
    if (strcmp(method, "client.reconnect") == 0) return CLIENT_RECONNECT;
    if (strcmp(method, "mining.ping") == 0) return MINING_PING;
    if (strcmp(method, "client.show_message") == 0) return CLIENT_SHOW_MESSAGE;
    if (strcmp(method, "client.get_version") == 0) return CLIENT_GET_VERSION;

    ESP_LOGI(TAG, "Unhandled method: %s", method);
    return METHOD_UNKNOWN;
}

static bool json_string_is_bounded(const cJSON *item, size_t min_length,
                                   size_t max_length)
{
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }

    size_t length = strlen(item->valuestring);
    return length >= min_length && length <= max_length;
}

static bool hex_string_is_bounded(const cJSON *item, size_t min_bytes,
                                  size_t max_bytes)
{
    if (!json_string_is_bounded(item, min_bytes * 2U, max_bytes * 2U)) {
        return false;
    }

    size_t length = strlen(item->valuestring);
    if ((length & 1U) != 0) {
        return false;
    }

    for (size_t i = 0; i < length; i++) {
        if (!isxdigit((unsigned char)item->valuestring[i])) {
            return false;
        }
    }
    return true;
}

static bool parse_hex_u32(const cJSON *item, uint32_t *value)
{
    if (value == NULL || !hex_string_is_bounded(item, 4, 4)) {
        return false;
    }

    char *end = NULL;
    unsigned long parsed = strtoul(item->valuestring, &end, 16);
    if (end == NULL || *end != '\0' || parsed > UINT32_MAX) {
        return false;
    }

    *value = (uint32_t)parsed;
    return true;
}

static bool json_integer_is_in_range(const cJSON *item, int minimum,
                                     int maximum, int *value)
{
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
        item->valuedouble < minimum || item->valuedouble > maximum ||
        item->valuedouble != trunc(item->valuedouble)) {
        return false;
    }

    if (value != NULL) {
        *value = (int)item->valuedouble;
    }
    return true;
}

static bool parse_mining_notify(cJSON *json, StratumApiV1Message *message)
{
    cJSON *params = cJSON_GetObjectItem(json, "params");
    if (!params || !cJSON_IsArray(params)) {
        ESP_LOGE(TAG, "Invalid params in mining.notify");
        return false;
    }

    int params_count = cJSON_GetArraySize(params);
    if (params_count < 9) {
        ESP_LOGE(TAG, "Not enough params in mining.notify: %d", params_count);
        return false;
    }

    cJSON *job_id = cJSON_GetArrayItem(params, 0);
    cJSON *prev_block_hash = cJSON_GetArrayItem(params, 1);
    cJSON *coinbase_1 = cJSON_GetArrayItem(params, 2);
    cJSON *coinbase_2 = cJSON_GetArrayItem(params, 3);
    cJSON *merkle_branch = cJSON_GetArrayItem(params, 4);
    cJSON *version = cJSON_GetArrayItem(params, 5);
    cJSON *target = cJSON_GetArrayItem(params, 6);
    cJSON *ntime = cJSON_GetArrayItem(params, 7);
    cJSON *clean_jobs = cJSON_GetArrayItem(params, params_count - 1);

    if (!json_string_is_bounded(job_id, 1, MAX_JOB_ID_LENGTH) ||
        !hex_string_is_bounded(prev_block_hash, HASH_SIZE, HASH_SIZE) ||
        !hex_string_is_bounded(coinbase_1, 1,
                               STRATUM_V1_MAX_JSON_LINE_SIZE / 2U) ||
        !hex_string_is_bounded(coinbase_2, 1,
                               STRATUM_V1_MAX_JSON_LINE_SIZE / 2U) ||
        !cJSON_IsArray(merkle_branch) || !cJSON_IsBool(clean_jobs)) {
        ESP_LOGE(TAG, "Invalid field type or length in mining.notify");
        return false;
    }

    size_t coinbase_hex_length = strlen(coinbase_1->valuestring) +
                                 strlen(coinbase_2->valuestring);
    if (coinbase_hex_length > STRATUM_V1_MAX_JSON_LINE_SIZE) {
        ESP_LOGE(TAG, "Coinbase fragments are too large in mining.notify");
        return false;
    }

    int merkle_count = cJSON_GetArraySize(merkle_branch);
    if (merkle_count < 0 || merkle_count > MAX_MERKLE_BRANCHES) {
        ESP_LOGE(TAG, "Invalid Merkle branch count: %d", merkle_count);
        return false;
    }

    for (int i = 0; i < merkle_count; i++) {
        if (!hex_string_is_bounded(cJSON_GetArrayItem(merkle_branch, i),
                                   HASH_SIZE, HASH_SIZE)) {
            ESP_LOGE(TAG, "Invalid Merkle branch at index %d", i);
            return false;
        }
    }

    uint32_t parsed_version;
    uint32_t parsed_target;
    uint32_t parsed_ntime;
    if (!parse_hex_u32(version, &parsed_version) ||
        !parse_hex_u32(target, &parsed_target) ||
        !parse_hex_u32(ntime, &parsed_ntime)) {
        ESP_LOGE(TAG, "Invalid version, target, or ntime in mining.notify");
        return false;
    }

    mining_notify *new_work = calloc(1, sizeof(mining_notify));
    if (new_work == NULL) {
        ESP_LOGE(TAG, "Memory allocation failed for mining.notify");
        return false;
    }

    new_work->job_id = strdup(job_id->valuestring);
    new_work->prev_block_hash = strdup(prev_block_hash->valuestring);
    new_work->coinbase_1 = strdup(coinbase_1->valuestring);
    new_work->coinbase_2 = strdup(coinbase_2->valuestring);
    new_work->n_merkle_branches = (size_t)merkle_count;
    if (merkle_count > 0) {
        new_work->merkle_branches = malloc(HASH_SIZE * (size_t)merkle_count);
    }

    if (new_work->job_id == NULL || new_work->prev_block_hash == NULL ||
        new_work->coinbase_1 == NULL || new_work->coinbase_2 == NULL ||
        (merkle_count > 0 && new_work->merkle_branches == NULL)) {
        ESP_LOGE(TAG, "Memory allocation failed while copying mining.notify");
        STRATUM_V1_free_mining_notify(new_work);
        return false;
    }

    for (int i = 0; i < merkle_count; i++) {
        cJSON *branch = cJSON_GetArrayItem(merkle_branch, i);
        if (hex2bin(branch->valuestring,
                    new_work->merkle_branches + HASH_SIZE * (size_t)i,
                    HASH_SIZE) != HASH_SIZE) {
            STRATUM_V1_free_mining_notify(new_work);
            return false;
        }
    }

    new_work->version = parsed_version;
    new_work->target = parsed_target;
    new_work->ntime = parsed_ntime;
    new_work->clean_jobs = cJSON_IsTrue(clean_jobs);

    message->mining_notification = new_work;
    ESP_LOGD(TAG, "Parsed mining.notify: job_id=%s, clean_jobs=%d", new_work->job_id, new_work->clean_jobs);
    return true;
}

static bool parse_set_difficulty(cJSON *json, StratumApiV1Message *message)
{
    cJSON *params = cJSON_GetObjectItem(json, "params");
    if (!params || !cJSON_IsArray(params) || cJSON_GetArraySize(params) == 0) {
        ESP_LOGE(TAG, "Invalid params for set_difficulty");
        return false;
    }
    cJSON *difficulty = cJSON_GetArrayItem(params, 0);
    if (!difficulty || !cJSON_IsNumber(difficulty) ||
        !isfinite(difficulty->valuedouble) || difficulty->valuedouble <= 0 ||
        difficulty->valuedouble > MAX_POOL_DIFFICULTY) {
        ESP_LOGE(TAG, "Invalid difficulty value in set_difficulty");
        return false;
    }
    message->new_difficulty = difficulty->valuedouble;
    ESP_LOGI(TAG, "Set pool difficulty: %.2f", message->new_difficulty);
    return true;
}

static bool parse_set_version_mask(cJSON *json, StratumApiV1Message *message)
{
    cJSON *params = cJSON_GetObjectItem(json, "params");
    if (!params || !cJSON_IsArray(params) || cJSON_GetArraySize(params) == 0) {
        ESP_LOGE(TAG, "Invalid params for set_version_mask");
        return false;
    }
    cJSON *mask = cJSON_GetArrayItem(params, 0);
    if (!parse_hex_u32(mask, &message->version_mask)) {
        ESP_LOGE(TAG, "Invalid version mask in set_version_mask");
        return false;
    }
    ESP_LOGI(TAG, "Set version mask: %08lx", message->version_mask);
    return true;
}

static bool parse_set_extranonce(cJSON *json, StratumApiV1Message *message)
{
    cJSON *params = cJSON_GetObjectItem(json, "params");
    if (!params || !cJSON_IsArray(params) || cJSON_GetArraySize(params) < 2) {
        ESP_LOGE(TAG, "Invalid params for set_extranonce");
        return false;
    }
    cJSON *extranonce1 = cJSON_GetArrayItem(params, 0);
    cJSON *extranonce2_size = cJSON_GetArrayItem(params, 1);
    int extranonce_2_len;
    if (!hex_string_is_bounded(extranonce1, 0, MAX_EXTRANONCE_1_BYTES) ||
        !json_integer_is_in_range(extranonce2_size, 0,
                                  MAX_EXTRANONCE_2_LEN,
                                  &extranonce_2_len)) {
        ESP_LOGE(TAG, "Invalid extranonce data in set_extranonce");
        return false;
    }
    message->extranonce_str = strdup(extranonce1->valuestring);
    if (message->extranonce_str == NULL) {
        ESP_LOGE(TAG, "Memory allocation failed for extranonce");
        return false;
    }
    message->extranonce_2_len = extranonce_2_len;
    ESP_LOGI(TAG, "Set extranonce: %s, size: %d", message->extranonce_str, message->extranonce_2_len);
    return true;
}

static bool parse_show_message(cJSON *json, StratumApiV1Message *message)
{
    cJSON *params = cJSON_GetObjectItem(json, "params");
    if (!params || !cJSON_IsArray(params) || cJSON_GetArraySize(params) == 0) {
        ESP_LOGE(TAG, "Invalid params for show_message");
        return false;
    }
    cJSON *msg = cJSON_GetArrayItem(params, 0);
    if (!json_string_is_bounded(msg, 0, MAX_POOL_MESSAGE_LEN)) {
        ESP_LOGE(TAG, "Invalid message in show_message");
        return false;
    }
    message->show_message = strdup(msg->valuestring);
    if (message->show_message == NULL) {
        ESP_LOGE(TAG, "Memory allocation failed for pool message");
        return false;
    }
    ESP_LOGI(TAG, "Pool message: %s", message->show_message);
    return true;
}

static bool parse_get_version(cJSON *json, StratumApiV1Message *message)
{
    message->version_string = strdup("unknown");
    if (message->version_string == NULL) {
        ESP_LOGE(TAG, "Memory allocation failed for version response");
        return false;
    }
    ESP_LOGI(TAG, "Get version requested");
    return true;
}

static bool parse_subscribe_result(cJSON *json, StratumApiV1Message *message)
{
    cJSON *result = cJSON_GetObjectItem(json, "result");
    cJSON *extranonce = cJSON_GetArrayItem(result, 1);
    cJSON *extranonce2_len = cJSON_GetArrayItem(result, 2);
    int parsed_extranonce_2_len;
    if (!hex_string_is_bounded(extranonce, 0, MAX_EXTRANONCE_1_BYTES) ||
        !json_integer_is_in_range(extranonce2_len, 0,
                                  MAX_EXTRANONCE_2_LEN,
                                  &parsed_extranonce_2_len)) {
        ESP_LOGE(TAG, "Invalid extranonce data in subscribe result");
        return false;
    }

    message->extranonce_str = strdup(extranonce->valuestring);
    if (message->extranonce_str == NULL) {
        ESP_LOGE(TAG, "Memory allocation failed for subscribe extranonce");
        return false;
    }
    message->extranonce_2_len = parsed_extranonce_2_len;
    message->response_success = true;
    ESP_LOGI(TAG, "Subscribe result: extranonce=%s, extranonce2_len=%d",
             message->extranonce_str, message->extranonce_2_len);
    return true;
}

static bool parse_configure_result(cJSON *json, StratumApiV1Message *message)
{
    cJSON *result = cJSON_GetObjectItem(json, "result");
    cJSON *version_rolling = cJSON_GetObjectItem(result, "version-rolling");
    cJSON *mask = cJSON_GetObjectItem(result, "version-rolling.mask");
    if (!version_rolling || !cJSON_IsTrue(version_rolling) ||
        !parse_hex_u32(mask, &message->version_mask)) {
        ESP_LOGE(TAG, "Invalid configure result fields");
        return false;
    }
    message->response_success = true;
    ESP_LOGI(TAG, "Configure result: version_mask=%08lx", message->version_mask);
    return true;
}

static bool set_response_error(StratumApiV1Message *message,
                               const cJSON *error_item)
{
    const char *error_text = "unknown";
    if (cJSON_IsString(error_item) && error_item->valuestring != NULL) {
        error_text = error_item->valuestring;
    }

    size_t error_length = strnlen(error_text, MAX_POOL_MESSAGE_LEN);
    char *bounded_error = strndup(error_text, error_length);
    if (bounded_error == NULL) {
        ESP_LOGE(TAG, "Memory allocation failed for Stratum error response");
        return false;
    }

    free(message->error_str);
    message->error_str = bounded_error;
    message->response_success = false;
    ESP_LOGI(TAG, "Result failed: %s", message->error_str);
    return true;
}

static bool parse_result(cJSON *json, StratumApiV1Message *message)
{
    cJSON *result = cJSON_GetObjectItem(json, "result");
    cJSON *error = cJSON_GetObjectItem(json, "error");
    cJSON *reject_reason = cJSON_GetObjectItem(json, "reject-reason");

    message->method = STRATUM_RESULT;

    // Handle error array format: [code, message, extra]
    if (error && cJSON_IsArray(error) && cJSON_GetArraySize(error) >= 2) {
        cJSON *error_msg = cJSON_GetArrayItem(error, 1);
        if (cJSON_IsString(error_msg)) {
            return set_response_error(message, error_msg);
        }
    } else if (error && cJSON_IsString(error)) {
        return set_response_error(message, error);
    } else if (error && cJSON_IsObject(error)) {
        cJSON *error_msg = cJSON_GetObjectItem(error, "message");
        if (error_msg && cJSON_IsString(error_msg)) {
            return set_response_error(message, error_msg);
        }
    }

    // Handle null result or non-null error
    if ((!result || cJSON_IsNull(result)) && (error && !cJSON_IsNull(error))) {
        return set_response_error(message, reject_reason);
    }

    // Handle boolean result
    if (cJSON_IsBool(result)) {
        message->response_success = cJSON_IsTrue(result);
        if (!message->response_success) {
            return set_response_error(message, reject_reason);
        } else {
            ESP_LOGI(TAG, "Result success");
        }
        return true;
    }

    // Handle subscribe result
    if (cJSON_IsArray(result) && cJSON_GetArraySize(result) >= 3) {
        message->method = STRATUM_RESULT_SUBSCRIBE;
        return parse_subscribe_result(json, message);
    }

    // Handle configure result
    if (cJSON_IsObject(result) && cJSON_GetObjectItem(result, "version-rolling")) {
        message->method = STRATUM_RESULT_CONFIGURE;
        return parse_configure_result(json, message);
    }

    ESP_LOGI(TAG, "Unhandled result format");
    return false;
}

bool STRATUM_V1_parse(StratumApiV1Message *message, const char *stratum_json)
{
    if (message == NULL || stratum_json == NULL) {
        return false;
    }

    STRATUM_V1_reset_message(message);

    size_t json_length = strnlen(stratum_json,
                                 STRATUM_V1_MAX_JSON_LINE_SIZE + 1U);
    if (json_length > STRATUM_V1_MAX_JSON_LINE_SIZE) {
        ESP_LOGE(TAG, "JSON-RPC message exceeds %u bytes",
                 STRATUM_V1_MAX_JSON_LINE_SIZE);
        return false;
    }

    ESP_LOGD(TAG, "rx: %.*s%s", (int)MIN(json_length, 512U), stratum_json,
             json_length > 512U ? "..." : "");

    cJSON *json = cJSON_ParseWithOpts(stratum_json, NULL, true);
    if (!cJSON_IsObject(json)) {
        ESP_LOGE(TAG, "JSON-RPC message is not a valid JSON object");
        message->method = METHOD_UNKNOWN;
        cJSON_Delete(json);
        return false;
    }

    // Parse message ID
    cJSON *id_json = cJSON_GetObjectItem(json, "id");
    if (id_json && !cJSON_IsNull(id_json)) {
        if (!json_integer_is_in_range(id_json, 0, INT_MAX,
                                      &message->message_id)) {
            ESP_LOGE(TAG, "Invalid JSON-RPC message id");
            cJSON_Delete(json);
            return false;
        }
    }

    // Parse method or result
    cJSON *method_json = cJSON_GetObjectItem(json, "method");
    message->method = parse_method(method_json);

    bool result = false;
    // Handle requests or results
    switch (message->method) {
        case STRATUM_RESULT:
            result = parse_result(json, message);
            break;
        case MINING_NOTIFY:
            result = parse_mining_notify(json, message);
            break;
        case MINING_SET_DIFFICULTY:
            result = parse_set_difficulty(json, message);
            break;
        case MINING_SET_VERSION_MASK:
            result = parse_set_version_mask(json, message);
            break;
        case MINING_SET_EXTRANONCE:
            result = parse_set_extranonce(json, message);
            break;
        case CLIENT_RECONNECT:
            ESP_LOGI(TAG, "Received client.reconnect");
            result = true;
            break;
        case MINING_PING:
            ESP_LOGI(TAG, "Received mining.ping");
            result = true;
            break;
        case CLIENT_SHOW_MESSAGE:
            result = parse_show_message(json, message);
            break;
        case CLIENT_GET_VERSION:
            result = parse_get_version(json, message);
            break;
        case METHOD_UNKNOWN:
            break;
        default:
            ESP_LOGI(TAG, "No handler for method: %d", message->method);
            break;
    }

    cJSON_Delete(json);
    return result;
}

void STRATUM_V1_free_mining_notify(mining_notify * mining_notify)
{
    free(mining_notify->job_id);
    free(mining_notify->prev_block_hash);
    free(mining_notify->coinbase_1);
    free(mining_notify->coinbase_2);
    free(mining_notify->merkle_branches);
    free(mining_notify);
}

static void stamp_tx(int request_id, uint64_t timestamp_us)
{
    if (request_id >= 1) {
        RequestTiming *timing = get_request_timing(request_id);
        if (timing) {
            timing->timestamp_us = timestamp_us;
            timing->tracking = true;
        }
    }
}

static void debug_stratum_tx(const char * msg)
{
    char *newline = strchr(msg, '\n');
    if (newline) {
        ESP_LOGI(TAG, "tx: %.*s", (int)(newline - msg), msg);
    } else {
        ESP_LOGI(TAG, "tx: %s", msg);
    }
}

int STRATUM_V1_subscribe(esp_transport_handle_t transport, int send_uid, const char * model)
{
    // Subscribe
    char subscribe_msg[BUFFER_SIZE];
    const esp_app_desc_t *app_desc = esp_app_get_description();
    const char *version = app_desc->version;	
    snprintf(subscribe_msg, sizeof(subscribe_msg),
        "{\"id\":%d,\"method\":\"mining.subscribe\",\"params\":[\"bitaxe/%s/%s\"]}\n",
        send_uid, model, version);
    debug_stratum_tx(subscribe_msg);

    return esp_transport_write(transport, subscribe_msg, strlen(subscribe_msg), TRANSPORT_TIMEOUT_MS);
}

int STRATUM_V1_suggest_difficulty(esp_transport_handle_t transport, int send_uid, uint32_t difficulty)
{
    char difficulty_msg[BUFFER_SIZE];
    snprintf(difficulty_msg, sizeof(difficulty_msg),
        "{\"id\":%d,\"method\":\"mining.suggest_difficulty\",\"params\":[%" PRIu32 "]}\n",
        send_uid, difficulty);
    debug_stratum_tx(difficulty_msg);

    return esp_transport_write(transport, difficulty_msg, strlen(difficulty_msg), TRANSPORT_TIMEOUT_MS);
}

int STRATUM_V1_extranonce_subscribe(esp_transport_handle_t transport, int send_uid)
{
    char extranonce_msg[BUFFER_SIZE];
    snprintf(extranonce_msg, sizeof(extranonce_msg),
        "{\"id\":%d,\"method\":\"mining.extranonce.subscribe\",\"params\":[]}\n",
        send_uid);
    debug_stratum_tx(extranonce_msg);

    return esp_transport_write(transport, extranonce_msg, strlen(extranonce_msg), TRANSPORT_TIMEOUT_MS);
}

int STRATUM_V1_authorize(esp_transport_handle_t transport, int send_uid, const char * username, const char * pass)
{
    char authorize_msg[BUFFER_SIZE];
    snprintf(authorize_msg, sizeof(authorize_msg),
        "{\"id\":%d,\"method\":\"mining.authorize\",\"params\":[\"%s\",\"%s\"]}\n",
        send_uid, username, pass);
    debug_stratum_tx(authorize_msg);

    return esp_transport_write(transport, authorize_msg, strlen(authorize_msg), TRANSPORT_TIMEOUT_MS);
}

int STRATUM_V1_pong(esp_transport_handle_t transport, int message_id)
{
    char pong_msg[BUFFER_SIZE];
    snprintf(pong_msg, sizeof(pong_msg),
        "{\"id\":%d,\"method\":\"pong\",\"params\":[]}\n",
        message_id);
    debug_stratum_tx(pong_msg);
    
    return esp_transport_write(transport, pong_msg, strlen(pong_msg), TRANSPORT_TIMEOUT_MS);
}

int STRATUM_V1_send_version(esp_transport_handle_t transport, int message_id)
{
    char version_msg[BUFFER_SIZE];
    const esp_app_desc_t *app_desc = esp_app_get_description();
    const char *version = app_desc->version;
    snprintf(version_msg, sizeof(version_msg),
        "{\"id\":%d,\"result\":\"%s\",\"error\":null}\n",
        message_id, version);
    debug_stratum_tx(version_msg);
    
    return esp_transport_write(transport, version_msg, strlen(version_msg), TRANSPORT_TIMEOUT_MS);
}

/// @param transport Transport to write to
/// @param send_uid Message ID
/// @param username The client’s user name.
/// @param job_id The job ID for the work being submitted.
/// @param extranonce_2 The hex-encoded value of extra nonce 2.
/// @param ntime The hex-encoded time value use in the block header.
/// @param nonce The hex-encoded nonce value to use in the block header.
/// @param version_bits The hex-encoded version bits set by miner (BIP310).
/// @param out_sent_time_us Pointer to store the time when the share was sent.
int STRATUM_V1_submit_share(esp_transport_handle_t transport, int send_uid, const char * username, const char * job_id,
                            const char * extranonce_2, const uint32_t ntime,
                            const uint32_t nonce, const uint32_t version_bits, uint64_t *out_sent_time_us)
{
    char submit_msg[BUFFER_SIZE];
    snprintf(submit_msg, sizeof(submit_msg),
        "{\"id\":%d,\"method\":\"mining.submit\",\"params\":[\"%s\",\"%s\",\"%s\",\"%08lx\",\"%08lx\",\"%08lx\"]}\n",
        send_uid, username, job_id, extranonce_2, ntime, nonce, version_bits);

    int ret = esp_transport_write(transport, submit_msg, strlen(submit_msg), TRANSPORT_TIMEOUT_MS);

    uint64_t now = esp_timer_get_time();
    if (out_sent_time_us) {
        *out_sent_time_us = now;
    }

    debug_stratum_tx(submit_msg);
    
    stamp_tx(send_uid, now);

    return ret;
}

int STRATUM_V1_configure_version_rolling(esp_transport_handle_t transport, int send_uid, uint32_t * version_mask)
{
    char configure_msg[BUFFER_SIZE];
    snprintf(configure_msg, sizeof(configure_msg),
        "{\"id\":%d,\"method\":\"mining.configure\",\"params\":[[\"version-rolling\"],{\"version-rolling.mask\":\"ffffffff\"}]}\n",
        send_uid);
    debug_stratum_tx(configure_msg);

    return esp_transport_write(transport, configure_msg, strlen(configure_msg), TRANSPORT_TIMEOUT_MS);
}
