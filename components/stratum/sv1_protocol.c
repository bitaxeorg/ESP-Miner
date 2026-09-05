#include "sv1_protocol.h"

#include "cJSON.h"
#include "esp_log.h"
#include "utils.h"

#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MIN_POOL_DIFFICULTY 0.0001
#define MAX_POOL_DIFFICULTY 4294967295.0
#define BITCOIN_GENESIS_NTIME 1231006505
#define MAX_ERROR_MSG_LEN 256

static const char *TAG = "sv1_protocol";

typedef struct
{
    char *buffer;
    size_t capacity;
    size_t length;
    bool failed;
} json_writer_t;

static bool json_writer_init(json_writer_t *writer, char *buffer, size_t capacity)
{
    if (buffer == NULL || capacity == 0) return false;

    writer->buffer = buffer;
    writer->capacity = capacity;
    writer->length = 0;
    writer->failed = false;
    buffer[0] = '\0';
    return true;
}

static void json_writer_append_bytes(json_writer_t *writer, const char *value, size_t length)
{
    if (writer->failed) return;

    size_t remaining = writer->capacity - writer->length;
    if (length >= remaining) {
        writer->failed = true;
        writer->buffer[0] = '\0';
        return;
    }

    memcpy(writer->buffer + writer->length, value, length);
    writer->length += length;
    writer->buffer[writer->length] = '\0';
}

static void json_writer_append_literal(json_writer_t *writer, const char *value)
{
    json_writer_append_bytes(writer, value, strlen(value));
}

static void json_writer_append_format(json_writer_t *writer, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

static void json_writer_append_format(json_writer_t *writer, const char *format, ...)
{
    if (writer->failed) return;

    size_t remaining = writer->capacity - writer->length;
    va_list args;
    va_start(args, format);
    int length = vsnprintf(writer->buffer + writer->length, remaining, format, args);
    va_end(args);

    if (length < 0 || (size_t)length >= remaining) {
        writer->failed = true;
        writer->buffer[0] = '\0';
        return;
    }

    writer->length += (size_t)length;
}

static void json_writer_append_escaped(json_writer_t *writer, const char *value)
{
    static const char hex_digits[] = "0123456789abcdef";

    for (const unsigned char *character = (const unsigned char *)value;
         *character != '\0'; ++character) {
        switch (*character) {
            case '"':
                json_writer_append_literal(writer, "\\\"");
                break;
            case '\\':
                json_writer_append_literal(writer, "\\\\");
                break;
            case '\b':
                json_writer_append_literal(writer, "\\b");
                break;
            case '\f':
                json_writer_append_literal(writer, "\\f");
                break;
            case '\n':
                json_writer_append_literal(writer, "\\n");
                break;
            case '\r':
                json_writer_append_literal(writer, "\\r");
                break;
            case '\t':
                json_writer_append_literal(writer, "\\t");
                break;
            default:
                if (*character < 0x20) {
                    char escape[] = {'\\', 'u', '0', '0',
                                     hex_digits[*character >> 4],
                                     hex_digits[*character & 0x0f]};
                    json_writer_append_bytes(writer, escape, sizeof(escape));
                } else {
                    json_writer_append_bytes(writer, (const char *)character, 1);
                }
                break;
        }
    }
}

static void json_writer_append_string(json_writer_t *writer, const char *value)
{
    json_writer_append_literal(writer, "\"");
    json_writer_append_escaped(writer, value);
    json_writer_append_literal(writer, "\"");
}

static int json_writer_finish(const json_writer_t *writer)
{
    if (writer->failed || writer->length > INT_MAX) {
        writer->buffer[0] = '\0';
        return -1;
    }

    return (int)writer->length;
}

int STRATUM_V1_encode_subscribe(char *buffer, size_t capacity, int message_id,
                                const char *model, const char *version)
{
    json_writer_t writer;
    if (!json_writer_init(&writer, buffer, capacity)) return -1;
    if (model == NULL || version == NULL) return -1;

    json_writer_append_format(&writer, "{\"id\":%d,\"method\":\"mining.subscribe\",\"params\":[\"bitaxe/", message_id);
    json_writer_append_escaped(&writer, model);
    json_writer_append_literal(&writer, "/");
    json_writer_append_escaped(&writer, version);
    json_writer_append_literal(&writer, "\"]}\n");
    return json_writer_finish(&writer);
}

int STRATUM_V1_encode_suggest_difficulty(char *buffer, size_t capacity, int message_id,
                                         uint32_t difficulty)
{
    json_writer_t writer;
    if (!json_writer_init(&writer, buffer, capacity)) return -1;

    json_writer_append_format(&writer,
                              "{\"id\":%d,\"method\":\"mining.suggest_difficulty\",\"params\":[%" PRIu32 "]}\n",
                              message_id, difficulty);
    return json_writer_finish(&writer);
}

int STRATUM_V1_encode_extranonce_subscribe(char *buffer, size_t capacity, int message_id)
{
    json_writer_t writer;
    if (!json_writer_init(&writer, buffer, capacity)) return -1;

    json_writer_append_format(&writer,
                              "{\"id\":%d,\"method\":\"mining.extranonce.subscribe\",\"params\":[]}\n",
                              message_id);
    return json_writer_finish(&writer);
}

int STRATUM_V1_encode_authorize(char *buffer, size_t capacity, int message_id,
                                const char *username, const char *password)
{
    json_writer_t writer;
    if (!json_writer_init(&writer, buffer, capacity)) return -1;
    if (username == NULL || password == NULL) return -1;

    json_writer_append_format(&writer,
                              "{\"id\":%d,\"method\":\"mining.authorize\",\"params\":[",
                              message_id);
    json_writer_append_string(&writer, username);
    json_writer_append_literal(&writer, ",");
    json_writer_append_string(&writer, password);
    json_writer_append_literal(&writer, "]}\n");
    return json_writer_finish(&writer);
}

int STRATUM_V1_encode_pong(char *buffer, size_t capacity, int message_id)
{
    json_writer_t writer;
    if (!json_writer_init(&writer, buffer, capacity)) return -1;

    json_writer_append_format(&writer, "{\"id\":%d,\"method\":\"pong\",\"params\":[]}\n", message_id);
    return json_writer_finish(&writer);
}

int STRATUM_V1_encode_version_response(char *buffer, size_t capacity, int message_id,
                                       const char *version)
{
    json_writer_t writer;
    if (!json_writer_init(&writer, buffer, capacity)) return -1;
    if (version == NULL) return -1;

    json_writer_append_format(&writer, "{\"id\":%d,\"result\":", message_id);
    json_writer_append_string(&writer, version);
    json_writer_append_literal(&writer, ",\"error\":null}\n");
    return json_writer_finish(&writer);
}

int STRATUM_V1_encode_submit_share(char *buffer, size_t capacity, int message_id,
                                   const char *username, const char *job_id,
                                   const char *extranonce_2, uint32_t ntime,
                                   uint32_t nonce, uint32_t version_bits)
{
    json_writer_t writer;
    if (!json_writer_init(&writer, buffer, capacity)) return -1;
    if (username == NULL || job_id == NULL || extranonce_2 == NULL) return -1;

    json_writer_append_format(&writer, "{\"id\":%d,\"method\":\"mining.submit\",\"params\":[", message_id);
    json_writer_append_string(&writer, username);
    json_writer_append_literal(&writer, ",");
    json_writer_append_string(&writer, job_id);
    json_writer_append_literal(&writer, ",");
    json_writer_append_string(&writer, extranonce_2);
    json_writer_append_format(&writer,
                              ",\"%08" PRIx32 "\",\"%08" PRIx32 "\",\"%08" PRIx32 "\"]}\n",
                              ntime, nonce, version_bits);
    return json_writer_finish(&writer);
}

int STRATUM_V1_encode_configure_version_rolling(char *buffer, size_t capacity, int message_id)
{
    json_writer_t writer;
    if (!json_writer_init(&writer, buffer, capacity)) return -1;

    json_writer_append_format(&writer,
                              "{\"id\":%d,\"method\":\"mining.configure\",\"params\":[[\"version-rolling\"],{\"version-rolling.mask\":\"ffffffff\"}]}\n",
                              message_id);
    return json_writer_finish(&writer);
}

void STRATUM_V1_reset_message(StratumApiV1Message *message)
{
    if (message == NULL) return;

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
    message->extranonce_2_len = 0;
    message->method = METHOD_UNKNOWN;
    message->message_id = -1;
    message->response_success = false;
    message->new_difficulty = 0.0;
    message->version_mask = 0;
}

static bool is_hex_digit(char character)
{
    return (character >= '0' && character <= '9') ||
           (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
}

static bool is_hex_string(const char *value, size_t minimum_length,
                          size_t maximum_length, bool require_even_length)
{
    size_t length = strlen(value);
    if (length < minimum_length || length > maximum_length ||
        (require_even_length && (length % 2 != 0))) {
        return false;
    }

    for (size_t index = 0; index < length; ++index) {
        if (!is_hex_digit(value[index])) return false;
    }
    return true;
}

static bool json_number_to_int(const cJSON *number, int *value)
{
    if (!number || !cJSON_IsNumber(number)) return false;

    double candidate = number->valuedouble;
    if (!isfinite(candidate) || candidate < INT_MIN || candidate > INT_MAX ||
        floor(candidate) != candidate) {
        return false;
    }

    *value = (int)candidate;
    return true;
}

static bool replace_bounded_string(char **destination, const char *source, size_t maximum_length)
{
    char *replacement = strndup(source, maximum_length);
    if (replacement == NULL) return false;

    free(*destination);
    *destination = replacement;
    return true;
}

static bool has_array_params(const cJSON *json)
{
    return cJSON_IsArray(cJSON_GetObjectItem(json, "params"));
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
    if (params_count < 9) {
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
    cJSON *clean_jobs_item = cJSON_GetArrayItem(params, 8);

    if (!cJSON_IsString(job_id_item) ||
        !cJSON_IsString(prev_hash_item) ||
        !cJSON_IsString(c1_item) ||
        !cJSON_IsString(c2_item) ||
        !cJSON_IsString(version_item) ||
        !cJSON_IsString(nbits_item) ||
        !cJSON_IsString(ntime_item) ||
        !cJSON_IsBool(clean_jobs_item)) {
        ESP_LOGE(TAG, "Invalid fields in mining.notify");
        return false;
    }

    size_t job_id_len = strlen(job_id_item->valuestring);
    if (job_id_len == 0 || job_id_len >= MAX_JOB_ID_LEN) {
        ESP_LOGE(TAG, "Invalid job_id length in mining.notify: %zu", job_id_len);
        return false;
    }

    if (!is_hex_string(prev_hash_item->valuestring, 64, 64, true)) {
        ESP_LOGE(TAG, "Invalid prev_hash in mining.notify");
        return false;
    }

    size_t c1_str_len = strlen(c1_item->valuestring);
    if (!is_hex_string(c1_item->valuestring, 2, MAX_COINBASE_PREFIX_LEN * 2, true)) {
        ESP_LOGE(TAG, "Invalid coinbase_1 hex in mining.notify: %zu characters", c1_str_len);
        return false;
    }

    size_t c2_str_len = strlen(c2_item->valuestring);
    if (!is_hex_string(c2_item->valuestring, 2, MAX_COINBASE_SUFFIX_LEN * 2, true)) {
        ESP_LOGE(TAG, "Invalid coinbase_2 hex in mining.notify: %zu characters", c2_str_len);
        return false;
    }

    if (!is_hex_string(version_item->valuestring, 8, 8, true)) {
        ESP_LOGE(TAG, "Invalid version hex in mining.notify");
        return false;
    }

    if (!is_hex_string(nbits_item->valuestring, 8, 8, true)) {
        ESP_LOGE(TAG, "Invalid nbits hex in mining.notify");
        return false;
    }

    if (!is_hex_string(ntime_item->valuestring, 8, 8, true)) {
        ESP_LOGE(TAG, "Invalid ntime hex in mining.notify");
        return false;
    }

    if (!cJSON_IsArray(merkle_branch)) {
        ESP_LOGE(TAG, "Invalid merkle_branch in mining.notify");
        return false;
    }

    size_t c1_len = c1_str_len / 2;
    size_t c2_len = c2_str_len / 2;
    size_t count = cJSON_GetArraySize(merkle_branch);
    if (count > MAX_MERKLE_BRANCHES) {
        ESP_LOGE(TAG, "Too many Merkle branches: %zu", count);
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        cJSON *branch = cJSON_GetArrayItem(merkle_branch, i);
        if (!cJSON_IsString(branch) ||
            !is_hex_string(branch->valuestring, 64, 64, true)) {
            ESP_LOGE(TAG, "Invalid Merkle branch at index %zu", i);
            return false;
        }
    }

    uint32_t version = (uint32_t)strtoul(version_item->valuestring, NULL, 16);
    uint32_t nbits = (uint32_t)strtoul(nbits_item->valuestring, NULL, 16);
    uint32_t ntime = (uint32_t)strtoul(ntime_item->valuestring, NULL, 16);

    if (ntime < BITCOIN_GENESIS_NTIME) {
        ESP_LOGW(TAG, "Rejecting notify with pre-genesis ntime: %" PRIu32, ntime);
        return false;
    }

    time_t now = time(NULL);
    if (now > 1704067200) { // Check future bound if NTP synced
        if ((uint64_t)ntime > (uint64_t)now + 7200) {
            ESP_LOGW(TAG, "Rejecting notify with ntime too far in future: %" PRIu32 " (now: %ld)",
                     ntime, (long)now);
            return false;
        }
    }

    uint8_t *prefix_buffer = job->coinbase_prefix;
    uint8_t *suffix_buffer = job->coinbase_suffix;
    if (prefix_buffer == NULL || suffix_buffer == NULL) {
        ESP_LOGE(TAG, "Missing mining.notify coinbase storage");
        return false;
    }

    memset(job, 0, sizeof(*job));
    job->coinbase_prefix = prefix_buffer;
    job->coinbase_suffix = suffix_buffer;
    job->type = JOB_TYPE_V1;
    strlcpy(job->job_id, job_id_item->valuestring, sizeof(job->job_id));
    hex2bin(prev_hash_item->valuestring, job->prev_hash, sizeof(job->prev_hash));
    reverse_endianness_per_word(job->prev_hash);
    hex2bin(c1_item->valuestring, job->coinbase_prefix, c1_len);
    job->coinbase_prefix_len = (uint16_t)c1_len;
    hex2bin(c2_item->valuestring, job->coinbase_suffix, c2_len);
    job->coinbase_suffix_len = (uint16_t)c2_len;
    job->merkle_path_count = (uint8_t)count;
    for (size_t i = 0; i < count; i++) {
        cJSON *branch = cJSON_GetArrayItem(merkle_branch, i);
        hex2bin(branch->valuestring, job->merkle_path[i], sizeof(job->merkle_path[i]));
    }
    job->version = version;
    job->nbits = nbits;
    job->ntime = ntime;
    job->clean_jobs = cJSON_IsTrue(clean_jobs_item);

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
    if (!mask || !cJSON_IsString(mask) ||
        !is_hex_string(mask->valuestring, 1, 8, false)) {
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
    int extranonce_2_len = 0;
    if (!extranonce1 || !cJSON_IsString(extranonce1) ||
        !json_number_to_int(extranonce2_size, &extranonce_2_len)) {
        ESP_LOGE(TAG, "Invalid extranonce data in set_extranonce");
        return false;
    }
    size_t e1_len = strlen(extranonce1->valuestring);
    if (!is_hex_string(extranonce1->valuestring, 0, 64, true)) {
        ESP_LOGE(TAG, "Invalid extranonce1 hex: %zu characters", e1_len);
        return false;
    }

    if (extranonce_2_len < 0 || extranonce_2_len > MAX_EXTRANONCE_2_LEN) {
        ESP_LOGW(TAG, "Invalid extranonce_2_len %d (clamping to 0..%d)",
                 extranonce_2_len, MAX_EXTRANONCE_2_LEN);
        extranonce_2_len = (extranonce_2_len < 0) ? 0 : MAX_EXTRANONCE_2_LEN;
    }
    if (!replace_bounded_string(&message->extranonce_str, extranonce1->valuestring, 64)) return false;
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
    bool replaced = replace_bounded_string(&message->show_message, msg->valuestring,
                                           MAX_POOL_MESSAGE_LEN);
    if (!replaced) return false;

    ESP_LOGI(TAG, "Pool message: %s", message->show_message);
    return true;
}

static bool parse_get_version(cJSON *json, StratumApiV1Message *message)
{
    if (!has_array_params(json)) {
        ESP_LOGE(TAG, "Invalid params for get_version");
        return false;
    }
    bool replaced = replace_bounded_string(&message->version_string, "unknown",
                                           sizeof("unknown") - 1);
    if (!replaced) return false;
    ESP_LOGI(TAG, "Get version requested");
    return true;
}

static bool parse_subscribe_result(cJSON *json, StratumApiV1Message *message)
{
    cJSON *result = cJSON_GetObjectItem(json, "result");
    cJSON *extranonce = cJSON_GetArrayItem(result, 1);
    cJSON *extranonce2_len = cJSON_GetArrayItem(result, 2);
    int extranonce_2_len = 0;
    if (!extranonce || !cJSON_IsString(extranonce) ||
        !json_number_to_int(extranonce2_len, &extranonce_2_len)) {
        ESP_LOGE(TAG, "Invalid extranonce data in subscribe result");
        return false;
    }

    size_t e1_len = strlen(extranonce->valuestring);
    if (!is_hex_string(extranonce->valuestring, 0, 64, true)) {
        ESP_LOGE(TAG, "Invalid subscribe extranonce hex: %zu characters", e1_len);
        return false;
    }

    if (extranonce_2_len < 0 || extranonce_2_len > MAX_EXTRANONCE_2_LEN) {
        ESP_LOGW(TAG, "Invalid extranonce_2_len %d in subscribe result (clamping to 0..%d)",
                 extranonce_2_len, MAX_EXTRANONCE_2_LEN);
        extranonce_2_len = (extranonce_2_len < 0) ? 0 : MAX_EXTRANONCE_2_LEN;
    }
    if (!replace_bounded_string(&message->extranonce_str, extranonce->valuestring, 64)) return false;
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
    if (!version_rolling || !cJSON_IsTrue(version_rolling) || !mask ||
        !cJSON_IsString(mask) || !is_hex_string(mask->valuestring, 1, 8, false)) {
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
            bool replaced = replace_bounded_string(&message->error_str, error_msg->valuestring,
                                                   MAX_ERROR_MSG_LEN);
            if (!replaced) return false;
            ESP_LOGI(TAG, "Result failed: %s", message->error_str);
            return true;
        }
    } else if (error && cJSON_IsString(error)) {
        message->response_success = false;
        bool replaced = replace_bounded_string(&message->error_str, error->valuestring,
                                               MAX_ERROR_MSG_LEN);
        if (!replaced) return false;
        ESP_LOGI(TAG, "Result failed: %s", message->error_str);
        return true;
    } else if (error && cJSON_IsObject(error)) {
        cJSON *error_msg = cJSON_GetObjectItem(error, "message");
        if (error_msg && cJSON_IsString(error_msg)) {
            message->response_success = false;
            bool replaced = replace_bounded_string(&message->error_str, error_msg->valuestring,
                                                   MAX_ERROR_MSG_LEN);
            if (!replaced) return false;
            ESP_LOGI(TAG, "Result failed: %s", message->error_str);
            return true;
        }
    }

    // Handle null result or non-null error
    if ((!result || cJSON_IsNull(result)) && (error && !cJSON_IsNull(error))) {
        message->response_success = false;
        const char *error_message = (reject_reason && cJSON_IsString(reject_reason))
            ? reject_reason->valuestring : "unknown";
        bool replaced = replace_bounded_string(&message->error_str, error_message,
                                               MAX_ERROR_MSG_LEN);
        if (!replaced) return false;
        ESP_LOGI(TAG, "Result failed: %s", message->error_str);
        return true;
    }

    // Handle boolean result
    if (cJSON_IsBool(result)) {
        message->response_success = cJSON_IsTrue(result);
        if (!message->response_success) {
            const char *error_message = (reject_reason && cJSON_IsString(reject_reason))
                ? reject_reason->valuestring : "unknown";
            bool replaced = replace_bounded_string(&message->error_str, error_message,
                                                   MAX_ERROR_MSG_LEN);
            if (!replaced) return false;
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

static bool parse_message_id(const cJSON *json, StratumApiV1Message *message)
{
    const cJSON *id = cJSON_GetObjectItem(json, "id");
    if (id == NULL || cJSON_IsNull(id)) return true;
    if (!cJSON_IsNumber(id) || !isfinite(id->valuedouble) ||
        id->valuedouble < INT_MIN || id->valuedouble > INT_MAX ||
        floor(id->valuedouble) != id->valuedouble) {
        return false;
    }

    message->message_id = (int)id->valuedouble;
    return true;
}

bool STRATUM_V1_parse(StratumApiV1Message *message, const char *stratum_json,
                      miner_job_t *job)
{
    if (message == NULL) return false;
    STRATUM_V1_reset_message(message);
    if (stratum_json == NULL) return false;
    message->job = job;

    ESP_LOGI(TAG, "rx: %s", stratum_json); // debug incoming stratum messages

    cJSON *json = cJSON_ParseWithOpts(stratum_json, NULL, true);
    if (!json) {
        ESP_LOGE(TAG, "JSON parse failed: %s", stratum_json);
        message->method = METHOD_UNKNOWN;
        return false;
    }

    if (!cJSON_IsObject(json) || !parse_message_id(json, message)) {
        ESP_LOGE(TAG, "Invalid SV1 message envelope");
        cJSON_Delete(json);
        return false;
    }

    // Parse method or result
    cJSON *method_json = cJSON_GetObjectItem(json, "method");
    if (method_json && !cJSON_IsString(method_json)) {
        ESP_LOGE(TAG, "Invalid SV1 method");
        cJSON_Delete(json);
        return false;
    }
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
            result = has_array_params(json);
            if (result) ESP_LOGI(TAG, "Received client.reconnect");
            break;
        case MINING_PING:
            result = has_array_params(json);
            if (result) ESP_LOGI(TAG, "Received mining.ping");
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
