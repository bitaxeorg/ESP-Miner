/******************************************************************************
 *  *
 * References:
 *  1. Stratum Protocol - [link](https://reference.cash/mining/stratum-protocol)
 *****************************************************************************/

#include "stratum_api.h"
#include "yyjson.h"
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
#include <sys/param.h>

#define TRANSPORT_TIMEOUT_MS 5000
#define BUFFER_SIZE 1024
#define MAX_EXTRANONCE_2_LEN 32
#define JSON_RPC_BUFFER_LIMIT (STRATUM_V1_MAX_JSON_LINE_SIZE + 2U)
#define MIN_POOL_DIFFICULTY 0.0001
#define MAX_POOL_DIFFICULTY 4294967295.0
#define BITCOIN_GENESIS_NTIME 1231006505
#define MAX_ERROR_MSG_LEN 256
static const char * TAG = "stratum_api";

static char * json_rpc_buffer = NULL;
static size_t json_rpc_buffer_size = 0;
static size_t json_rpc_buffer_len = 0;

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

bool STRATUM_V1_initialize_buffer(void)
{
    // Free any existing buffer (may be non-NULL if a previous V1 task was running)
    free(json_rpc_buffer);
    json_rpc_buffer = NULL;
    json_rpc_buffer_size = 0;
    json_rpc_buffer_len = 0;

    json_rpc_buffer = malloc(BUFFER_SIZE);
    if (json_rpc_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for JSON-RPC buffer");
        return false;
    }
    json_rpc_buffer_size = BUFFER_SIZE;
    json_rpc_buffer[0] = '\0';

    return true;
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
    message->job = NULL;
    message->method = METHOD_UNKNOWN;
    message->message_id = -1;
    message->response_success = false;
    message->new_difficulty = 0.0;
    message->version_mask = 0;
}

static stratum_method parse_method(yyjson_val *method_json)
{
    if (!method_json || !yyjson_is_str(method_json)) {
        return STRATUM_RESULT;
    }

    const char *method = yyjson_get_str(method_json);
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

static bool parse_mining_notify(yyjson_val *root, miner_job_t *job)
{
    if (!job) {
        ESP_LOGE(TAG, "NULL job destination in mining.notify");
        return false;
    }

    yyjson_val *params = yyjson_obj_get(root, "params");
    if (!params || !yyjson_is_arr(params)) {
        ESP_LOGE(TAG, "Invalid params in mining.notify");
        return false;
    }
    size_t params_count = yyjson_arr_size(params);
    if (params_count < 8) {
        ESP_LOGE(TAG, "Not enough params in mining.notify: %zu", params_count);
        return false;
    }

    yyjson_val *job_id_item = yyjson_arr_get(params, 0);
    yyjson_val *prev_hash_item = yyjson_arr_get(params, 1);
    yyjson_val *c1_item = yyjson_arr_get(params, 2);
    yyjson_val *c2_item = yyjson_arr_get(params, 3);
    yyjson_val *merkle_branch = yyjson_arr_get(params, 4);
    yyjson_val *version_item = yyjson_arr_get(params, 5);
    yyjson_val *nbits_item = yyjson_arr_get(params, 6);
    yyjson_val *ntime_item = yyjson_arr_get(params, 7);

    if (!job_id_item || !yyjson_is_str(job_id_item) ||
        !prev_hash_item || !yyjson_is_str(prev_hash_item) ||
        !c1_item || !yyjson_is_str(c1_item) ||
        !c2_item || !yyjson_is_str(c2_item) ||
        !version_item || !yyjson_is_str(version_item) ||
        !nbits_item || !yyjson_is_str(nbits_item) ||
        !ntime_item || !yyjson_is_str(ntime_item)) {
        ESP_LOGE(TAG, "Invalid string fields in mining.notify");
        return false;
    }

    const char *job_id_str = yyjson_get_str(job_id_item);
    if (job_id_str[0] == '\0') {
        ESP_LOGE(TAG, "Empty job_id in mining.notify");
        return false;
    }

    const char *prev_hash_str = yyjson_get_str(prev_hash_item);
    if (strlen(prev_hash_str) != 64) {
        ESP_LOGE(TAG, "Invalid prev_hash length in mining.notify (expected 64, got %zu)",
                 strlen(prev_hash_str));
        return false;
    }

    const char *c1_str = yyjson_get_str(c1_item);
    size_t c1_str_len = strlen(c1_str);
    if (c1_str_len == 0 || (c1_str_len % 2) != 0) {
        ESP_LOGE(TAG, "Invalid coinbase_1 hex length in mining.notify: %zu", c1_str_len);
        return false;
    }

    const char *c2_str = yyjson_get_str(c2_item);
    size_t c2_str_len = strlen(c2_str);
    if (c2_str_len == 0 || (c2_str_len % 2) != 0) {
        ESP_LOGE(TAG, "Invalid coinbase_2 hex length in mining.notify: %zu", c2_str_len);
        return false;
    }

    const char *version_str = yyjson_get_str(version_item);
    if (strlen(version_str) != 8) {
        ESP_LOGE(TAG, "Invalid version hex length in mining.notify (expected 8, got %zu)",
                 strlen(version_str));
        return false;
    }

    const char *nbits_str = yyjson_get_str(nbits_item);
    if (strlen(nbits_str) != 8) {
        ESP_LOGE(TAG, "Invalid nbits hex length in mining.notify (expected 8, got %zu)",
                 strlen(nbits_str));
        return false;
    }

    const char *ntime_str = yyjson_get_str(ntime_item);
    if (strlen(ntime_str) != 8) {
        ESP_LOGE(TAG, "Invalid ntime hex length in mining.notify (expected 8, got %zu)",
                 strlen(ntime_str));
        return false;
    }

    if (!merkle_branch || !yyjson_is_arr(merkle_branch)) {
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

    if (strlen(job_id_str) >= sizeof(job->job_id)) {
        ESP_LOGE(TAG, "Invalid job_id length in mining.notify (expected < %zu, got %zu)",
                 sizeof(job->job_id), strlen(job_id_str));
        return false;
    }
    strlcpy(job->job_id, job_id_str, sizeof(job->job_id));

    hex2bin(prev_hash_str, job->prev_hash, 32);
    reverse_endianness_per_word(job->prev_hash);

    size_t c1_len = c1_str_len / 2;
    if (c1_len > MAX_COINBASE_PREFIX_LEN) {
        ESP_LOGE(TAG, "coinbase_1 length %zu exceeds maximum %d in mining.notify", c1_len, MAX_COINBASE_PREFIX_LEN);
        return false;
    }
    hex2bin(c1_str, job->coinbase_prefix, c1_len);
    job->coinbase_prefix_len = (uint16_t)c1_len;

    size_t c2_len = c2_str_len / 2;
    if (c2_len > MAX_COINBASE_SUFFIX_LEN) {
        ESP_LOGE(TAG, "coinbase_2 length %zu exceeds maximum %d in mining.notify", c2_len, MAX_COINBASE_SUFFIX_LEN);
        return false;
    }
    hex2bin(c2_str, job->coinbase_suffix, c2_len);
    job->coinbase_suffix_len = (uint16_t)c2_len;

    size_t count = yyjson_arr_size(merkle_branch);
    if (count > MAX_MERKLE_BRANCHES) {
        ESP_LOGE(TAG, "Too many Merkle branches: %zu", count);
        return false;
    }
    job->merkle_path_count = (uint8_t)count;
    for (size_t i = 0; i < count; i++) {
        yyjson_val *branch = yyjson_arr_get(merkle_branch, i);
        if (!branch || !yyjson_is_str(branch) || strlen(yyjson_get_str(branch)) != 64) {
            ESP_LOGE(TAG, "Invalid Merkle branch at index %zu", i);
            return false;
        }
        hex2bin(yyjson_get_str(branch), job->merkle_path[i], 32);
    }

    job->version = strtoul(version_str, NULL, 16);
    job->nbits = strtoul(nbits_str, NULL, 16);
    job->ntime = strtoul(ntime_str, NULL, 16);
    job->clean_jobs = yyjson_is_true(yyjson_arr_get(params, params_count - 1));

    if (job->nbits == 0) {
        ESP_LOGW(TAG, "Rejecting notify with zero nbits");
        return false;
    }

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

static bool parse_set_difficulty(yyjson_val *root, StratumApiV1Message *message)
{
    yyjson_val *params = yyjson_obj_get(root, "params");
    if (!params || !yyjson_is_arr(params) || yyjson_arr_size(params) == 0) {
        ESP_LOGE(TAG, "Invalid params for set_difficulty");
        return false;
    }
    yyjson_val *difficulty = yyjson_arr_get(params, 0);
    if (!difficulty || !yyjson_is_num(difficulty)) {
        ESP_LOGE(TAG, "Invalid difficulty value in set_difficulty");
        return false;
    }
    double diff_val = yyjson_get_num(difficulty);
    if (isnan(diff_val) || isinf(diff_val) || diff_val < MIN_POOL_DIFFICULTY || diff_val > MAX_POOL_DIFFICULTY) {
        ESP_LOGE(TAG, "Rejecting out-of-range pool difficulty: %f", diff_val);
        return false;
    }
    message->new_difficulty = diff_val;
    ESP_LOGI(TAG, "Set pool difficulty: %.2f", message->new_difficulty);
    return true;
}

static bool parse_set_version_mask(yyjson_val *root, StratumApiV1Message *message)
{
    yyjson_val *params = yyjson_obj_get(root, "params");
    if (!params || !yyjson_is_arr(params) || yyjson_arr_size(params) == 0) {
        ESP_LOGE(TAG, "Invalid params for set_version_mask");
        return false;
    }
    yyjson_val *mask = yyjson_arr_get(params, 0);
    if (!mask || !yyjson_is_str(mask)) {
        ESP_LOGE(TAG, "Invalid version mask in set_version_mask");
        return false;
    }
    uint32_t raw_mask = (uint32_t)strtoul(yyjson_get_str(mask), NULL, 16);
    if ((raw_mask & ~BIP320_VERSION_ROLLING_MASK) != 0) {
        ESP_LOGW(TAG, "Mask 0x%08" PRIx32 " contains non-BIP320 bits; masking to allowed range", raw_mask);
    }
    message->version_mask = raw_mask & BIP320_VERSION_ROLLING_MASK;
    ESP_LOGI(TAG, "Set version mask: %08" PRIx32, message->version_mask);
    return true;
}

static bool parse_set_extranonce(yyjson_val *root, StratumApiV1Message *message)
{
    yyjson_val *params = yyjson_obj_get(root, "params");
    if (!params || !yyjson_is_arr(params) || yyjson_arr_size(params) < 2) {
        ESP_LOGE(TAG, "Invalid params for set_extranonce");
        return false;
    }
    yyjson_val *extranonce1 = yyjson_arr_get(params, 0);
    yyjson_val *extranonce2_size = yyjson_arr_get(params, 1);
    if (!extranonce1 || !extranonce2_size || !yyjson_is_str(extranonce1) || !yyjson_is_int(extranonce2_size)) {
        ESP_LOGE(TAG, "Invalid extranonce data in set_extranonce");
        return false;
    }
    const char *e1_str = yyjson_get_str(extranonce1);
    size_t e1_len = strlen(e1_str);
    if (e1_len % 2 != 0 || e1_len > 64) {
        ESP_LOGE(TAG, "Invalid extranonce1 hex length: %zu", e1_len);
        return false;
    }
    if (message->extranonce_str) free(message->extranonce_str);
    message->extranonce_str = strdup(e1_str);

    int64_t raw_e2 = yyjson_get_sint(extranonce2_size);
    if (raw_e2 < 0 || raw_e2 > MAX_EXTRANONCE_2_LEN) {
        ESP_LOGW(TAG, "Invalid extranonce_2_len %" PRId64 " (clamping to 0..%d)",
                 raw_e2, MAX_EXTRANONCE_2_LEN);
        raw_e2 = (raw_e2 < 0) ? 0 : MAX_EXTRANONCE_2_LEN;
    }
    message->extranonce_2_len = (int)raw_e2;
    ESP_LOGI(TAG, "Set extranonce: %s, size: %d", message->extranonce_str, message->extranonce_2_len);
    return true;
}

static bool parse_show_message(yyjson_val *root, StratumApiV1Message *message)
{
    yyjson_val *params = yyjson_obj_get(root, "params");
    if (!params || !yyjson_is_arr(params) || yyjson_arr_size(params) == 0) {
        ESP_LOGE(TAG, "Invalid params for show_message");
        return false;
    }
    yyjson_val *msg = yyjson_arr_get(params, 0);
    if (!msg || !yyjson_is_str(msg)) {
        ESP_LOGE(TAG, "Invalid message in show_message");
        return false;
    }
    if (message->show_message) free(message->show_message);
    message->show_message = strndup(yyjson_get_str(msg), MAX_POOL_MESSAGE_LEN);

    ESP_LOGI(TAG, "Pool message: %s", message->show_message);
    return true;
}

static bool parse_get_version(yyjson_val *root, StratumApiV1Message *message)
{
    (void)root;
    if (message->version_string) free(message->version_string);
    message->version_string = strdup("unknown");
    ESP_LOGI(TAG, "Get version requested");
    return true;
}

static bool parse_subscribe_result(yyjson_val *root, StratumApiV1Message *message)
{
    yyjson_val *result = yyjson_obj_get(root, "result");
    yyjson_val *extranonce = yyjson_arr_get(result, 1);
    yyjson_val *extranonce2_len = yyjson_arr_get(result, 2);
    if (!extranonce || !extranonce2_len || !yyjson_is_str(extranonce) || !yyjson_is_int(extranonce2_len)) {
        ESP_LOGE(TAG, "Invalid extranonce data in subscribe result");
        return false;
    }

    const char *e1_str = yyjson_get_str(extranonce);
    size_t e1_len = strlen(e1_str);
    if (e1_len % 2 != 0 || e1_len > 64) {
        ESP_LOGE(TAG, "Invalid subscribe extranonce hex length: %zu", e1_len);
        return false;
    }

    if (message->extranonce_str) free(message->extranonce_str);
    message->extranonce_str = strdup(e1_str);

    int64_t raw_e2 = yyjson_get_sint(extranonce2_len);
    if (raw_e2 < 0 || raw_e2 > MAX_EXTRANONCE_2_LEN) {
        ESP_LOGW(TAG, "Invalid extranonce_2_len %" PRId64 " in subscribe result (clamping to 0..%d)", 
                 raw_e2, MAX_EXTRANONCE_2_LEN);
        raw_e2 = (raw_e2 < 0) ? 0 : MAX_EXTRANONCE_2_LEN;
    }
    message->extranonce_2_len = (int)raw_e2;
    message->response_success = true;
    ESP_LOGI(TAG, "Subscribe result: extranonce=%s, extranonce2_len=%d",
             message->extranonce_str, message->extranonce_2_len);
    return true;
}

static bool parse_configure_result(yyjson_val *root, StratumApiV1Message *message)
{
    yyjson_val *result = yyjson_obj_get(root, "result");
    yyjson_val *version_rolling = yyjson_obj_get(result, "version-rolling");
    yyjson_val *mask = yyjson_obj_get(result, "version-rolling.mask");
    if (!version_rolling || !yyjson_is_true(version_rolling) || !mask || !yyjson_is_str(mask)) {
        ESP_LOGE(TAG, "Invalid configure result fields");
        return false;
    }
    uint32_t raw_mask = (uint32_t)strtoul(yyjson_get_str(mask), NULL, 16);
    if ((raw_mask & ~BIP320_VERSION_ROLLING_MASK) != 0) {
        ESP_LOGW(TAG, "Configure mask 0x%08" PRIx32 " contains non-BIP320 bits; masking to allowed range", raw_mask);
    }
    message->version_mask = raw_mask & BIP320_VERSION_ROLLING_MASK;
    message->response_success = true;
    ESP_LOGI(TAG, "Configure result: version_mask=%08" PRIx32, message->version_mask);
    return true;
}

static bool parse_result(yyjson_val *root, StratumApiV1Message *message)
{
    yyjson_val *result = yyjson_obj_get(root, "result");
    yyjson_val *error = yyjson_obj_get(root, "error");
    yyjson_val *reject_reason = yyjson_obj_get(root, "reject-reason");

    message->method = STRATUM_RESULT;

    // Handle error array format: [code, message, extra]
    if (error && yyjson_is_arr(error) && yyjson_arr_size(error) >= 2) {
        yyjson_val *error_msg = yyjson_arr_get(error, 1);
        if (yyjson_is_str(error_msg)) {
            message->response_success = false;
            if (message->error_str) free(message->error_str);
            message->error_str = strndup(yyjson_get_str(error_msg), MAX_ERROR_MSG_LEN);
            ESP_LOGI(TAG, "Result failed: %s", message->error_str);
            return true;
        }
    } else if (error && yyjson_is_str(error)) {
        message->response_success = false;
        if (message->error_str) free(message->error_str);
        message->error_str = strndup(yyjson_get_str(error), MAX_ERROR_MSG_LEN);
        ESP_LOGI(TAG, "Result failed: %s", message->error_str);
        return true;
    } else if (error && yyjson_is_obj(error)) {
        yyjson_val *error_msg = yyjson_obj_get(error, "message");
        if (error_msg && yyjson_is_str(error_msg)) {
            message->response_success = false;
            if (message->error_str) free(message->error_str);
            message->error_str = strndup(yyjson_get_str(error_msg), MAX_ERROR_MSG_LEN);
            ESP_LOGI(TAG, "Result failed: %s", message->error_str);
            return true;
        }
    }

    // Handle null result or non-null error
    if ((!result || yyjson_is_null(result)) && (error && !yyjson_is_null(error))) {
        message->response_success = false;
        if (message->error_str) free(message->error_str);
        message->error_str = (reject_reason && yyjson_is_str(reject_reason))
            ? strndup(yyjson_get_str(reject_reason), MAX_ERROR_MSG_LEN)
            : strdup("unknown");
        ESP_LOGI(TAG, "Result failed: %s", message->error_str);
        return true;
    }

    // Handle boolean result
    if (yyjson_is_bool(result)) {
        message->response_success = yyjson_is_true(result);
        if (!message->response_success) {
            if (message->error_str) free(message->error_str);
            message->error_str = (reject_reason && yyjson_is_str(reject_reason))
                ? strndup(yyjson_get_str(reject_reason), MAX_ERROR_MSG_LEN)
                : strdup("unknown");
            ESP_LOGI(TAG, "Result failed: %s", message->error_str);
        } else {
            ESP_LOGI(TAG, "Result success");
        }
        return true;
    }

    // Handle subscribe result
    if (yyjson_is_arr(result) && yyjson_arr_size(result) >= 3) {
        message->method = STRATUM_RESULT_SUBSCRIBE;
        return parse_subscribe_result(root, message);
    }

    // Handle configure result
    if (yyjson_is_obj(result) && yyjson_obj_get(result, "version-rolling")) {
        message->method = STRATUM_RESULT_CONFIGURE;
        return parse_configure_result(root, message);
    }

    ESP_LOGI(TAG, "Unhandled result format");
    return false;
}

bool STRATUM_V1_parse(StratumApiV1Message *message, const char *stratum_json, miner_job_t *job)
{
    if (message == NULL || stratum_json == NULL) {
        return false;
    }

    STRATUM_V1_reset_message(message);
    message->job = job;

    size_t json_length = strnlen(stratum_json, STRATUM_V1_MAX_JSON_LINE_SIZE + 1U);
    if (json_length > STRATUM_V1_MAX_JSON_LINE_SIZE) {
        ESP_LOGE(TAG, "JSON-RPC message exceeds %u bytes", STRATUM_V1_MAX_JSON_LINE_SIZE);
        return false;
    }

    ESP_LOGD(TAG, "rx: %.*s%s", (int)MIN(json_length, 512U), stratum_json,
             json_length > 512U ? "..." : "");

    yyjson_doc *doc = yyjson_read(stratum_json, json_length, YYJSON_READ_NOFLAG);
    if (!doc) {
        ESP_LOGE(TAG, "JSON-RPC message is not valid JSON: %s", stratum_json);
        message->method = METHOD_UNKNOWN;
        return false;
    }

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        ESP_LOGE(TAG, "JSON-RPC message is not a valid JSON object: %s", stratum_json);
        message->method = METHOD_UNKNOWN;
        yyjson_doc_free(doc);
        return false;
    }

    // Parse message ID
    yyjson_val *id_json = yyjson_obj_get(root, "id");
    if (id_json && !yyjson_is_null(id_json)) {
        if (!yyjson_is_int(id_json)) {
            ESP_LOGE(TAG, "Invalid JSON-RPC message id");
            yyjson_doc_free(doc);
            return false;
        }
        if (yyjson_is_sint(id_json)) {
            int64_t sval = yyjson_get_sint(id_json);
            if (sval < 0 || sval > INT_MAX) {
                ESP_LOGE(TAG, "Invalid JSON-RPC message id");
                yyjson_doc_free(doc);
                return false;
            }
            message->message_id = (int)sval;
        } else if (yyjson_is_uint(id_json)) {
            uint64_t uval = yyjson_get_uint(id_json);
            if (uval > INT_MAX) {
                ESP_LOGE(TAG, "Invalid JSON-RPC message id");
                yyjson_doc_free(doc);
                return false;
            }
            message->message_id = (int)uval;
        }
    }

    // Parse method or result
    yyjson_val *method_json = yyjson_obj_get(root, "method");
    message->method = parse_method(method_json);

    bool result = false;
    // Handle requests or results
    switch (message->method) {
        case STRATUM_RESULT:
            result = parse_result(root, message);
            break;
        case MINING_NOTIFY:
            result = parse_mining_notify(root, job);
            break;
        case MINING_SET_DIFFICULTY:
            result = parse_set_difficulty(root, message);
            break;
        case MINING_SET_VERSION_MASK:
            result = parse_set_version_mask(root, message);
            break;
        case MINING_SET_EXTRANONCE:
            result = parse_set_extranonce(root, message);
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
            result = parse_show_message(root, message);
            break;
        case CLIENT_GET_VERSION:
            result = parse_get_version(root, message);
            break;
        case METHOD_UNKNOWN:
            break;
        default:
            ESP_LOGI(TAG, "No handler for method: %d", message->method);
            break;
    }

    yyjson_doc_free(doc);
    return result;
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

    if (out_sent_time_us) *out_sent_time_us = esp_timer_get_time();

    debug_stratum_tx(submit_msg);

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
