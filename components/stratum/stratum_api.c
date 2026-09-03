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
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#define TRANSPORT_TIMEOUT_MS 5000
#define BUFFER_SIZE 1024
#define MAX_EXTRANONCE_2_LEN 32
#define MAX_JSON_RPC_BUFFER_SIZE (32 * 1024)
#define MIN_POOL_DIFFICULTY 0.0001
#define MAX_POOL_DIFFICULTY 4294967295.0
#define BITCOIN_GENESIS_NTIME 1231006505
#define MAX_ERROR_MSG_LEN 256
static const char * TAG = "stratum_api";

static char * json_rpc_buffer = NULL;
static size_t json_rpc_buffer_size = 0;

static RequestTiming *request_timings = NULL;

static RequestTiming* get_request_timing(int request_id) {
    if (request_id < 0) return NULL;
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

esp_transport_handle_t STRATUM_V1_transport_init(tls_mode tls, const char * cert)
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

void STRATUM_V1_initialize_buffer(void)
{
    // Free any existing buffer (may be non-NULL if a previous V1 task was running)
    free(json_rpc_buffer);

    json_rpc_buffer = malloc(BUFFER_SIZE);
    json_rpc_buffer_size = BUFFER_SIZE;
    if (json_rpc_buffer == NULL) {
        printf("Error: Failed to allocate memory for buffer\n");
        exit(1);
    }
    memset(json_rpc_buffer, 0, BUFFER_SIZE);

    if (request_timings == NULL) {
        request_timings = heap_caps_malloc(sizeof(RequestTiming) * MAX_REQUEST_IDS, MALLOC_CAP_SPIRAM);
        if (request_timings == NULL) {
            printf("Error: Failed to allocate memory for request_timings\n");
            exit(1);
        }
    }

    for (int i = 0; i < MAX_REQUEST_IDS; i++) {
        request_timings[i].timestamp_us = 0;
        request_timings[i].tracking = false;
    }
}

void cleanup_stratum_buffer()
{
    free(json_rpc_buffer);
    json_rpc_buffer = NULL;
    if (request_timings) {
        free(request_timings);
        request_timings = NULL;
    }
}

static bool realloc_json_buffer(size_t len)
{
    size_t old, new;

    old = strlen(json_rpc_buffer);
    new = old + len + 1;

    if (new < json_rpc_buffer_size) {
        return true;
    }

    if (new > MAX_JSON_RPC_BUFFER_SIZE) {
        ESP_LOGE(TAG, "JSON-RPC line exceeds maximum buffer size (%d bytes)", MAX_JSON_RPC_BUFFER_SIZE);
        return false;
    }

    new = new + (BUFFER_SIZE - (new % BUFFER_SIZE));
    void * new_sockbuf = realloc(json_rpc_buffer, new);

    if (new_sockbuf == NULL) {
        ESP_LOGE(TAG, "Error: realloc failed in realloc_json_buffer");
        return false;
    }

    json_rpc_buffer = new_sockbuf;
    memset(json_rpc_buffer + old, 0, new - old);
    json_rpc_buffer_size = new;
    return true;
}

char * STRATUM_V1_receive_jsonrpc_line(esp_transport_handle_t transport)
{
    if (json_rpc_buffer == NULL) {
        STRATUM_V1_initialize_buffer();
    }
    char *line = NULL;
    char recv_buffer[BUFFER_SIZE];
    int nbytes;

    while (!strstr(json_rpc_buffer, "\n")) {
        memset(recv_buffer, 0, BUFFER_SIZE);
        nbytes = esp_transport_read(transport, recv_buffer, BUFFER_SIZE - 1, TRANSPORT_TIMEOUT_MS);
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
            if (json_rpc_buffer) {
                free(json_rpc_buffer);
                json_rpc_buffer = NULL;
            }
            return NULL;
        }
        if (nbytes > 0) {
            if (!realloc_json_buffer(nbytes)) {
                free(json_rpc_buffer);
                json_rpc_buffer = NULL;
                return NULL;
            }
            strncat(json_rpc_buffer, recv_buffer, nbytes);
        }
    }

    // Extract the line
    size_t buflen = strlen(json_rpc_buffer);
    char *newline_pos = strchr(json_rpc_buffer, '\n');
    if (newline_pos) {
        size_t line_len = newline_pos - json_rpc_buffer;
        line = strndup(json_rpc_buffer, line_len);  // Copy only up to \n
        size_t remaining_len = buflen - line_len - 1;
        if (remaining_len > 0) {
            memmove(json_rpc_buffer, newline_pos + 1, remaining_len);
            json_rpc_buffer[remaining_len] = '\0';
        } else {
            json_rpc_buffer[0] = '\0';
        }
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
    message->job = NULL;
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

static bool parse_mining_notify(cJSON *json, miner_job_t *job)
{
    if (!job) {
        ESP_LOGE(TAG, "NULL job destination in mining.notify");
        return false;
    }

    cJSON *params = cJSON_GetObjectItem(json, "params");
    if (!params || !cJSON_IsArray(params)) {
        ESP_LOGE(TAG, "Invalid params in mining.notify");
        return false;
    }
    int params_count = cJSON_GetArraySize(params);
    if (params_count < 8) {
        ESP_LOGE(TAG, "Not enough params in mining.notify: %d", params_count);
        return false;
    }

    cJSON *job_id_item = cJSON_GetArrayItem(params, 0);
    cJSON *prev_hash_item = cJSON_GetArrayItem(params, 1);
    cJSON *c1_item = cJSON_GetArrayItem(params, 2);
    cJSON *c2_item = cJSON_GetArrayItem(params, 3);
    cJSON *merkle_branch = cJSON_GetArrayItem(params, 4);
    cJSON *version_item = cJSON_GetArrayItem(params, 5);
    cJSON *nbits_item = cJSON_GetArrayItem(params, 6);
    cJSON *ntime_item = cJSON_GetArrayItem(params, 7);

    if (!job_id_item || !cJSON_IsString(job_id_item) ||
        !prev_hash_item || !cJSON_IsString(prev_hash_item) ||
        !c1_item || !cJSON_IsString(c1_item) ||
        !c2_item || !cJSON_IsString(c2_item) ||
        !version_item || !cJSON_IsString(version_item) ||
        !nbits_item || !cJSON_IsString(nbits_item) ||
        !ntime_item || !cJSON_IsString(ntime_item)) {
        ESP_LOGE(TAG, "Invalid string fields in mining.notify");
        return false;
    }

    if (job_id_item->valuestring[0] == '\0') {
        ESP_LOGE(TAG, "Empty job_id in mining.notify");
        return false;
    }

    if (strlen(prev_hash_item->valuestring) != 64) {
        ESP_LOGE(TAG, "Invalid prev_hash length in mining.notify (expected 64, got %zu)",
                 strlen(prev_hash_item->valuestring));
        return false;
    }

    size_t c1_str_len = strlen(c1_item->valuestring);
    if (c1_str_len == 0 || (c1_str_len % 2) != 0) {
        ESP_LOGE(TAG, "Invalid coinbase_1 hex length in mining.notify: %zu", c1_str_len);
        return false;
    }

    size_t c2_str_len = strlen(c2_item->valuestring);
    if (c2_str_len == 0 || (c2_str_len % 2) != 0) {
        ESP_LOGE(TAG, "Invalid coinbase_2 hex length in mining.notify: %zu", c2_str_len);
        return false;
    }

    if (strlen(version_item->valuestring) != 8) {
        ESP_LOGE(TAG, "Invalid version hex length in mining.notify (expected 8, got %zu)",
                 strlen(version_item->valuestring));
        return false;
    }

    if (strlen(nbits_item->valuestring) != 8) {
        ESP_LOGE(TAG, "Invalid nbits hex length in mining.notify (expected 8, got %zu)",
                 strlen(nbits_item->valuestring));
        return false;
    }

    if (strlen(ntime_item->valuestring) != 8) {
        ESP_LOGE(TAG, "Invalid ntime hex length in mining.notify (expected 8, got %zu)",
                 strlen(ntime_item->valuestring));
        return false;
    }

    if (!merkle_branch || !cJSON_IsArray(merkle_branch)) {
        ESP_LOGE(TAG, "Invalid merkle_branch in mining.notify");
        return false;
    }

    if (!job->coinbase_prefix) {
        job->coinbase_prefix = heap_caps_calloc(1, MAX_COINBASE_PREFIX_LEN, MALLOC_CAP_SPIRAM);
        if (!job->coinbase_prefix) job->coinbase_prefix = calloc(1, MAX_COINBASE_PREFIX_LEN);
    }
    if (!job->coinbase_suffix) {
        job->coinbase_suffix = heap_caps_calloc(1, MAX_COINBASE_SUFFIX_LEN, MALLOC_CAP_SPIRAM);
        if (!job->coinbase_suffix) job->coinbase_suffix = calloc(1, 2048);
    }
    uint8_t *p_buf = job->coinbase_prefix;
    uint8_t *s_buf = job->coinbase_suffix;
    memset(job, 0, sizeof(miner_job_t));
    job->coinbase_prefix = p_buf;
    job->coinbase_suffix = s_buf;
    job->type = JOB_TYPE_V1;

    if (strlen(job_id_item->valuestring) >= sizeof(job->job_id)) {
        ESP_LOGE(TAG, "Invalid job_id length in mining.notify (expected < %zu, got %zu)",
                 sizeof(job->job_id), strlen(job_id_item->valuestring));
        return false;
    }
    strlcpy(job->job_id, job_id_item->valuestring, sizeof(job->job_id));

    hex2bin(prev_hash_item->valuestring, job->prev_hash, 32);
    reverse_endianness_per_word(job->prev_hash);

    size_t c1_len = c1_str_len / 2;
    if (c1_len > MAX_COINBASE_PREFIX_LEN) {
        ESP_LOGE(TAG, "coinbase_1 length %zu exceeds maximum %d in mining.notify", c1_len, MAX_COINBASE_PREFIX_LEN);
        return false;
    }
    hex2bin(c1_item->valuestring, job->coinbase_prefix, c1_len);
    job->coinbase_prefix_len = (uint16_t)c1_len;

    size_t c2_len = c2_str_len / 2;
    if (c2_len > MAX_COINBASE_SUFFIX_LEN) {
        ESP_LOGE(TAG, "coinbase_2 length %zu exceeds maximum %d in mining.notify", c2_len, MAX_COINBASE_SUFFIX_LEN);
        return false;
    }
    hex2bin(c2_item->valuestring, job->coinbase_suffix, c2_len);
    job->coinbase_suffix_len = (uint16_t)c2_len;

    size_t count = cJSON_GetArraySize(merkle_branch);
    if (count > MAX_MERKLE_BRANCHES) {
        ESP_LOGE(TAG, "Too many Merkle branches: %zu", count);
        return false;
    }
    job->merkle_path_count = (uint8_t)count;
    for (size_t i = 0; i < count; i++) {
        cJSON *branch = cJSON_GetArrayItem(merkle_branch, i);
        if (!branch || !cJSON_IsString(branch) || strlen(branch->valuestring) != 64) {
            ESP_LOGE(TAG, "Invalid Merkle branch at index %zu", i);
            return false;
        }
        hex2bin(branch->valuestring, job->merkle_path[i], 32);
    }

    job->version = strtoul(version_item->valuestring, NULL, 16);
    job->nbits = strtoul(nbits_item->valuestring, NULL, 16);
    job->ntime = strtoul(ntime_item->valuestring, NULL, 16);
    job->clean_jobs = cJSON_IsTrue(cJSON_GetArrayItem(params, params_count - 1));

    if (job->ntime < BITCOIN_GENESIS_NTIME) {
        ESP_LOGW(TAG, "Rejecting notify with pre-genesis ntime: %" PRIu32, job->ntime);
        return false;
    }

    time_t now = time(NULL);
    if (now > 1704067200) { // Check future bound if NTP synced
        if (job->ntime > (uint32_t)now + 7200) {
            ESP_LOGW(TAG, "Rejecting notify with ntime too far in future: %" PRIu32 " (now: %ld)",
                     job->ntime, (long)now);
            return false;
        }
    }

    ESP_LOGD(TAG, "Parsed mining.notify: job_id=%s, clean_jobs=%d", job->job_id, job->clean_jobs);
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
    if (!difficulty || !cJSON_IsNumber(difficulty)) {
        ESP_LOGE(TAG, "Invalid difficulty value in set_difficulty");
        return false;
    }
    double diff_val = difficulty->valuedouble;
    if (isnan(diff_val) || isinf(diff_val) || diff_val < MIN_POOL_DIFFICULTY || diff_val > MAX_POOL_DIFFICULTY) {
        ESP_LOGE(TAG, "Rejecting out-of-range pool difficulty: %f", diff_val);
        return false;
    }
    message->new_difficulty = diff_val;
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
    if (!mask || !cJSON_IsString(mask)) {
        ESP_LOGE(TAG, "Invalid version mask in set_version_mask");
        return false;
    }
    uint32_t raw_mask = (uint32_t)strtoul(mask->valuestring, NULL, 16);
    if ((raw_mask & ~BIP320_VERSION_ROLLING_MASK) != 0) {
        ESP_LOGW(TAG, "Mask 0x%08" PRIx32 " contains non-BIP320 bits; masking to allowed range", raw_mask);
    }
    message->version_mask = raw_mask & BIP320_VERSION_ROLLING_MASK;
    ESP_LOGI(TAG, "Set version mask: %08" PRIx32, message->version_mask);
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
    if (!extranonce1 || !extranonce2_size || !cJSON_IsString(extranonce1) || !cJSON_IsNumber(extranonce2_size)) {
        ESP_LOGE(TAG, "Invalid extranonce data in set_extranonce");
        return false;
    }
    size_t e1_len = strlen(extranonce1->valuestring);
    if (e1_len % 2 != 0 || e1_len > 64) {
        ESP_LOGE(TAG, "Invalid extranonce1 hex length: %zu", e1_len);
        return false;
    }
    if (message->extranonce_str) free(message->extranonce_str);
    message->extranonce_str = strdup(extranonce1->valuestring);
    
    int extranonce_2_len = extranonce2_size->valueint;
    if (extranonce_2_len < 0 || extranonce_2_len > MAX_EXTRANONCE_2_LEN) {
        ESP_LOGW(TAG, "Invalid extranonce_2_len %d (clamping to 0..%d)",
                 extranonce_2_len, MAX_EXTRANONCE_2_LEN);
        extranonce_2_len = (extranonce_2_len < 0) ? 0 : MAX_EXTRANONCE_2_LEN;
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
    if (!msg || !cJSON_IsString(msg)) {
        ESP_LOGE(TAG, "Invalid message in show_message");
        return false;
    }
    if (message->show_message) free(message->show_message);
    message->show_message = strndup(msg->valuestring, MAX_POOL_MESSAGE_LEN);
    
    ESP_LOGI(TAG, "Pool message: %s", message->show_message);
    return true;
}

static bool parse_get_version(cJSON *json, StratumApiV1Message *message)
{
    if (message->version_string) free(message->version_string);
    message->version_string = strdup("unknown");
    ESP_LOGI(TAG, "Get version requested");
    return true;
}

static bool parse_subscribe_result(cJSON *json, StratumApiV1Message *message)
{
    cJSON *result = cJSON_GetObjectItem(json, "result");
    cJSON *extranonce = cJSON_GetArrayItem(result, 1);
    cJSON *extranonce2_len = cJSON_GetArrayItem(result, 2);
    if (!extranonce || !extranonce2_len || !cJSON_IsString(extranonce) || !cJSON_IsNumber(extranonce2_len)) {
        ESP_LOGE(TAG, "Invalid extranonce data in subscribe result");
        return false;
    }

    size_t e1_len = strlen(extranonce->valuestring);
    if (e1_len % 2 != 0 || e1_len > 64) {
        ESP_LOGE(TAG, "Invalid subscribe extranonce hex length: %zu", e1_len);
        return false;
    }

    if (message->extranonce_str) free(message->extranonce_str);
    message->extranonce_str = strdup(extranonce->valuestring);
    
    int extranonce_2_len = extranonce2_len->valueint;
    if (extranonce_2_len < 0 || extranonce_2_len > MAX_EXTRANONCE_2_LEN) {
        ESP_LOGW(TAG, "Invalid extranonce_2_len %d in subscribe result (clamping to 0..%d)", 
                 extranonce_2_len, MAX_EXTRANONCE_2_LEN);
        extranonce_2_len = (extranonce_2_len < 0) ? 0 : MAX_EXTRANONCE_2_LEN;
    }
    message->extranonce_2_len = extranonce_2_len;
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
    if (!version_rolling || !cJSON_IsTrue(version_rolling) || !mask || !cJSON_IsString(mask)) {
        ESP_LOGE(TAG, "Invalid configure result fields");
        return false;
    }
    uint32_t raw_mask = (uint32_t)strtoul(mask->valuestring, NULL, 16);
    if ((raw_mask & ~BIP320_VERSION_ROLLING_MASK) != 0) {
        ESP_LOGW(TAG, "Configure mask 0x%08" PRIx32 " contains non-BIP320 bits; masking to allowed range", raw_mask);
    }
    message->version_mask = raw_mask & BIP320_VERSION_ROLLING_MASK;
    message->response_success = true;
    ESP_LOGI(TAG, "Configure result: version_mask=%08" PRIx32, message->version_mask);
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
            message->response_success = false;
            if (message->error_str) free(message->error_str);
            message->error_str = strndup(error_msg->valuestring, MAX_ERROR_MSG_LEN);
            ESP_LOGI(TAG, "Result failed: %s", message->error_str);
            return true;
        }
    } else if (error && cJSON_IsString(error)) {
        message->response_success = false;
        if (message->error_str) free(message->error_str);
        message->error_str = strndup(error->valuestring, MAX_ERROR_MSG_LEN);
        ESP_LOGI(TAG, "Result failed: %s", message->error_str);
        return true;
    } else if (error && cJSON_IsObject(error)) {
        cJSON *error_msg = cJSON_GetObjectItem(error, "message");
        if (error_msg && cJSON_IsString(error_msg)) {
            message->response_success = false;
            if (message->error_str) free(message->error_str);
            message->error_str = strndup(error_msg->valuestring, MAX_ERROR_MSG_LEN);
            ESP_LOGI(TAG, "Result failed: %s", message->error_str);
            return true;
        }
    }

    // Handle null result or non-null error
    if ((!result || cJSON_IsNull(result)) && (error && !cJSON_IsNull(error))) {
        message->response_success = false;
        if (message->error_str) free(message->error_str);
        message->error_str = (reject_reason && cJSON_IsString(reject_reason))
            ? strndup(reject_reason->valuestring, MAX_ERROR_MSG_LEN)
            : strdup("unknown");
        ESP_LOGI(TAG, "Result failed: %s", message->error_str);
        return true;
    }

    // Handle boolean result
    if (cJSON_IsBool(result)) {
        message->response_success = cJSON_IsTrue(result);
        if (!message->response_success) {
            if (message->error_str) free(message->error_str);
            message->error_str = (reject_reason && cJSON_IsString(reject_reason))
                ? strndup(reject_reason->valuestring, MAX_ERROR_MSG_LEN)
                : strdup("unknown");
            ESP_LOGI(TAG, "Result failed: %s", message->error_str);
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

bool STRATUM_V1_parse(StratumApiV1Message *message, const char *stratum_json, miner_job_t *job)
{
    STRATUM_V1_reset_message(message);
    message->job = job;

    ESP_LOGI(TAG, "rx: %s", stratum_json); // debug incoming stratum messages

    cJSON *json = cJSON_Parse(stratum_json);
    if (!json) {
        ESP_LOGE(TAG, "JSON parse failed: %s", stratum_json);
        message->method = METHOD_UNKNOWN;
        return false;
    }

    // Parse message ID
    cJSON *id_json = cJSON_GetObjectItem(json, "id");
    if (id_json && cJSON_IsNumber(id_json)) {
        message->message_id = id_json->valueint;
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
            result = parse_mining_notify(json, job);
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

stratum_protocol_t stratum_protocol_from_string(const char *s)
{
    if (!s) return STRATUM_PROTOCOL_UNKNOWN;
    if (strcmp(s, STRATUM_V1) == 0) return STRATUM_PROTOCOL_V1;
    if (strcmp(s, STRATUM_V2) == 0) return STRATUM_PROTOCOL_V2;
    return STRATUM_PROTOCOL_UNKNOWN;
}

const char *stratum_protocol_to_string(stratum_protocol_t p)
{
    switch (p) {
        case STRATUM_PROTOCOL_V1: return STRATUM_V1;
        case STRATUM_PROTOCOL_V2: return STRATUM_V2;
        default: return "unknown";
    }
}
