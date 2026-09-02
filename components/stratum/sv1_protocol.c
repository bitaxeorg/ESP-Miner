#include "sv1_protocol.h"

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "utils.h"

#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/param.h>

#define MIN_POOL_DIFFICULTY 0.0001
#define MAX_POOL_DIFFICULTY 4294967295.0
#define BITCOIN_GENESIS_NTIME 1231006505
#define MAX_ERROR_MSG_LEN 256

static const char *TAG = "sv1_protocol";

static int validate_encoded_message(char *buffer, size_t capacity, int length)
{
    if (length < 0 || (size_t)length >= capacity) {
        buffer[0] = '\0';
        return -1;
    }

    return length;
}

int STRATUM_V1_encode_subscribe(char *buffer, size_t capacity, int message_id,
                                const char *model, const char *version)
{
    if (buffer == NULL || capacity == 0 || model == NULL || version == NULL) {
        return -1;
    }
    int length = snprintf(buffer, capacity,
                          "{\"id\":%d,\"method\":\"mining.subscribe\",\"params\":[\"bitaxe/%s/%s\"]}\n",
                          message_id, model, version);
    return validate_encoded_message(buffer, capacity, length);
}

int STRATUM_V1_encode_suggest_difficulty(char *buffer, size_t capacity, int message_id,
                                         uint32_t difficulty)
{
    if (buffer == NULL || capacity == 0) return -1;
    int length = snprintf(buffer, capacity,
                          "{\"id\":%d,\"method\":\"mining.suggest_difficulty\",\"params\":[%" PRIu32 "]}\n",
                          message_id, difficulty);
    return validate_encoded_message(buffer, capacity, length);
}

int STRATUM_V1_encode_extranonce_subscribe(char *buffer, size_t capacity, int message_id)
{
    if (buffer == NULL || capacity == 0) return -1;
    int length = snprintf(buffer, capacity,
                          "{\"id\":%d,\"method\":\"mining.extranonce.subscribe\",\"params\":[]}\n",
                          message_id);
    return validate_encoded_message(buffer, capacity, length);
}

int STRATUM_V1_encode_authorize(char *buffer, size_t capacity, int message_id,
                                const char *username, const char *password)
{
    if (buffer == NULL || capacity == 0 || username == NULL || password == NULL) {
        return -1;
    }
    int length = snprintf(buffer, capacity,
                          "{\"id\":%d,\"method\":\"mining.authorize\",\"params\":[\"%s\",\"%s\"]}\n",
                          message_id, username, password);
    return validate_encoded_message(buffer, capacity, length);
}

int STRATUM_V1_encode_pong(char *buffer, size_t capacity, int message_id)
{
    if (buffer == NULL || capacity == 0) return -1;
    int length = snprintf(buffer, capacity,
                          "{\"id\":%d,\"method\":\"pong\",\"params\":[]}\n",
                          message_id);
    return validate_encoded_message(buffer, capacity, length);
}

int STRATUM_V1_encode_version_response(char *buffer, size_t capacity, int message_id,
                                       const char *version)
{
    if (buffer == NULL || capacity == 0 || version == NULL) {
        return -1;
    }
    int length = snprintf(buffer, capacity,
                          "{\"id\":%d,\"result\":\"%s\",\"error\":null}\n",
                          message_id, version);
    return validate_encoded_message(buffer, capacity, length);
}

int STRATUM_V1_encode_submit_share(char *buffer, size_t capacity, int message_id,
                                   const char *username, const char *job_id,
                                   const char *extranonce_2, uint32_t ntime,
                                   uint32_t nonce, uint32_t version_bits)
{
    if (buffer == NULL || capacity == 0 || username == NULL || job_id == NULL || extranonce_2 == NULL) {
        return -1;
    }
    int length = snprintf(buffer, capacity,
                          "{\"id\":%d,\"method\":\"mining.submit\",\"params\":[\"%s\",\"%s\",\"%s\",\"%08" PRIx32 "\",\"%08" PRIx32 "\",\"%08" PRIx32 "\"]}\n",
                          message_id, username, job_id, extranonce_2, ntime, nonce, version_bits);
    return validate_encoded_message(buffer, capacity, length);
}

int STRATUM_V1_encode_configure_version_rolling(char *buffer, size_t capacity, int message_id)
{
    if (buffer == NULL || capacity == 0) return -1;
    int length = snprintf(buffer, capacity,
                          "{\"id\":%d,\"method\":\"mining.configure\",\"params\":[[\"version-rolling\"],{\"version-rolling.mask\":\"ffffffff\"}]}\n",
                          message_id);
    return validate_encoded_message(buffer, capacity, length);
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
    (void)json;
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

    cJSON *json = cJSON_ParseWithOpts(stratum_json, NULL, true);
    if (!cJSON_IsObject(json)) {
        ESP_LOGE(TAG, "JSON-RPC message is not a valid JSON object: %s", stratum_json);
        message->method = METHOD_UNKNOWN;
        cJSON_Delete(json);
        return false;
    }

    // Parse message ID
    cJSON *id_json = cJSON_GetObjectItem(json, "id");
    if (id_json && !cJSON_IsNull(id_json)) {
        if (!cJSON_IsNumber(id_json) || id_json->valuedouble < 0 ||
            id_json->valuedouble > INT_MAX ||
            id_json->valuedouble != (double)id_json->valueint) {
            ESP_LOGE(TAG, "Invalid JSON-RPC message id");
            cJSON_Delete(json);
            return false;
        }
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
        case STRATUM_RESULT_SUBSCRIBE:
        case STRATUM_RESULT_CONFIGURE:
        case METHOD_UNKNOWN:
            break;
    }

    cJSON_Delete(json);
    return result;
}
