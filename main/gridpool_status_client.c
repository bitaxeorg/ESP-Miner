#include "gridpool_status_client.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#include "asic.h"
#include "nvs_config.h"
#include "system.h"
#include "gridpool_direct_builder.h"
#include "utils.h"

#define GRIDPOOL_POLL_INTERVAL_MS 60000
#define GRIDPOOL_DISABLED_POLL_INTERVAL_MS 30000
#define GRIDPOOL_ERROR_POLL_INTERVAL_MS 15000
#define GRIDPOOL_TEMPLATE_INITIAL_DELAY_MS 15000
#define GRIDPOOL_TEMPLATE_PROBE_INTERVAL_MS 60000
#define GRIDPOOL_TEMPLATE_ERROR_INTERVAL_MS 30000
#define GRIDPOOL_TEMPLATE_UNCONFIGURED_INTERVAL_MS 30000
#define GRIDPOOL_HTTP_TIMEOUT_MS 10000
#define GRIDPOOL_RPC_TIMEOUT_MS 30000
#define GRIDPOOL_INITIAL_RESPONSE_BYTES 2048
#define GRIDPOOL_MAX_RESPONSE_BYTES 131072
#define GRIDPOOL_MAX_TEMPLATE_RESPONSE_BYTES (5 * 1024 * 1024)
#define GRIDPOOL_MAX_URL_BYTES 512
#define GRIDPOOL_TEMPLATE_MAX_PAYOUT_OUTPUTS 512
#define GRIDPOOL_TEMPLATE_TAG "/GridPool Direct ESP32/"
#define GRIDPOOL_DIRECT_STATUS_POLL_INTERVAL_MS 15000
#define GRIDPOOL_DIRECT_TEMPLATE_REFRESH_MS 60000
#define GRIDPOOL_DIRECT_RETRY_DELAY_MS 5000
#define GRIDPOOL_DIRECT_MIN_SHARE_INTERVAL_MS 1000
#define GRIDPOOL_SHARE_RESPONSE_BYTES 4096
#define GRIDPOOL_REWARD_MODE_SPLIT "split"
#define GRIDPOOL_REWARD_MODE_SOLO "solo"

static const char *TAG = "gridpool_status";

static SemaphoreHandle_t payout_cache_mutex = NULL;
static SemaphoreHandle_t gridpool_http_mutex = NULL;
static gridpool_direct_payout_output_t *payout_cache_outputs = NULL;
static size_t payout_cache_output_count = 0;
static uint64_t payout_cache_sequence = 0;
static uint32_t payout_cache_response_bytes = 0;
static bool payout_cache_valid = false;
static int64_t last_gridpool_share_submit_us = 0;
static portMUX_TYPE direct_refresh_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t direct_refresh_generation = 1;

typedef struct {
    gridpool_direct_template_t template_data;
    uint8_t *merkle_path;
    size_t merkle_path_count;
    gridpool_direct_payout_output_t *outputs;
    size_t output_count;
    uint64_t payout_sequence;
    uint32_t payout_response_bytes;
    uint32_t refresh_generation;
    int64_t fetched_us;
    bool valid;
    bool split_rewards;
} gridpool_direct_template_context_t;

typedef struct {
    gridpool_direct_job_t job;
    gridpool_direct_tx_bundle_t *tx_bundle;
    char miner_address[GRIDPOOL_DIRECT_ADDRESS_MAX_LEN];
    char username[GRIDPOOL_DIRECT_ADDRESS_MAX_LEN];
    uint64_t payout_sequence;
    uint32_t refresh_generation;
    double submit_difficulty;
    bool submit_shares;
    bool split_rewards;
} gridpool_direct_work_context_t;

typedef struct {
    char *data;
    int len;
    int capacity;
    int max_bytes;
    int content_length;
    bool overflow;
} gridpool_http_response_t;

static void set_status(SystemModule *module, bool reachable, int http_status, const char *status, const char *error)
{
    module->gridpool_reachable = reachable;
    module->gridpool_http_status = http_status;
    snprintf(module->gridpool_status, sizeof(module->gridpool_status), "%s", status ? status : "");
    snprintf(module->gridpool_last_error, sizeof(module->gridpool_last_error), "%s", error ? error : "");
}

static void set_template_status(SystemModule *module, bool reachable, int http_status, const char *status, const char *error)
{
    module->gridpool_template_reachable = reachable;
    module->gridpool_template_http_status = http_status;
    snprintf(module->gridpool_template_status, sizeof(module->gridpool_template_status), "%s", status ? status : "");
    snprintf(module->gridpool_template_last_error, sizeof(module->gridpool_template_last_error), "%s", error ? error : "");
}

static int64_t now_seconds(void)
{
    return esp_timer_get_time() / 1000000;
}

static void *response_malloc(size_t size)
{
    void *ptr = NULL;
    if (esp_psram_is_initialized()) {
        ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    return ptr ? ptr : malloc(size);
}

static void *response_realloc(void *ptr, size_t size)
{
    void *next = NULL;
    if (esp_psram_is_initialized()) {
        next = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!next) {
            next = heap_caps_realloc(ptr, size, MALLOC_CAP_8BIT);
        }
    } else {
        next = realloc(ptr, size);
    }
    return next;
}

static esp_err_t take_gridpool_http_mutex(void)
{
    if (!gridpool_http_mutex) {
        return ESP_OK;
    }
    return xSemaphoreTake(gridpool_http_mutex, pdMS_TO_TICKS(GRIDPOOL_HTTP_TIMEOUT_MS + 5000)) == pdTRUE
        ? ESP_OK
        : ESP_ERR_TIMEOUT;
}

static void give_gridpool_http_mutex(void)
{
    if (gridpool_http_mutex) {
        xSemaphoreGive(gridpool_http_mutex);
    }
}

static uint32_t gridpool_direct_get_refresh_generation(void)
{
    taskENTER_CRITICAL(&direct_refresh_mux);
    uint32_t generation = direct_refresh_generation;
    taskEXIT_CRITICAL(&direct_refresh_mux);
    return generation;
}

static void gridpool_direct_request_template_refresh(void)
{
    taskENTER_CRITICAL(&direct_refresh_mux);
    direct_refresh_generation++;
    if (direct_refresh_generation == 0) {
        direct_refresh_generation = 1;
    }
    taskEXIT_CRITICAL(&direct_refresh_mux);
}

static void clear_runtime_status(SystemModule *module)
{
    module->gridpool_reachable = false;
    module->gridpool_http_status = 0;
    module->gridpool_last_success_time = 0;
    module->gridpool_last_attempt_time = 0;
    module->gridpool_payout_sequence = 0;
    module->gridpool_advice_sequence = 0;
    module->gridpool_payout_count = 0;
    module->gridpool_coinbase_output_count = 0;
    module->gridpool_current_round = 0;
    module->gridpool_shared_winner_slot_count = 0;
    module->gridpool_on_deck_count = 0;
    module->gridpool_open_on_deck_slots = 0;
    module->gridpool_current_tip_height = 0;
    module->gridpool_minimum_difficulty_to_enter = 0;
    module->gridpool_best_on_deck_difficulty = 0;
    module->gridpool_current_on_deck_floor_difficulty = 0;
    module->gridpool_winner_slots = 0;
    module->gridpool_winner_value_sats = 0;
    module->gridpool_template_reachable = false;
    module->gridpool_template_http_status = 0;
    module->gridpool_template_last_success_time = 0;
    module->gridpool_template_last_attempt_time = 0;
    module->gridpool_template_runs = 0;
    module->gridpool_template_failures = 0;
    module->gridpool_template_height = 0;
    module->gridpool_template_tx_count = 0;
    module->gridpool_template_merkle_path_count = 0;
    module->gridpool_template_coinbase_bytes = 0;
    module->gridpool_template_bytes = 0;
    module->gridpool_template_payout_bytes = 0;
    module->gridpool_template_gridpool_outputs = 0;
    module->gridpool_template_total_ms = 0;
    module->gridpool_template_rpc_ms = 0;
    module->gridpool_template_payout_ms = 0;
    module->gridpool_template_parse_ms = 0;
    module->gridpool_template_build_ms = 0;
    module->gridpool_template_heap_before = 0;
    module->gridpool_template_heap_after = 0;
    module->gridpool_template_internal_heap_before = 0;
    module->gridpool_template_internal_heap_after = 0;
    module->gridpool_template_spiram_heap_before = 0;
    module->gridpool_template_spiram_heap_after = 0;
    module->gridpool_template_slot0_value_sats = 0;
    module->gridpool_direct_jobs_sent = 0;
    module->gridpool_share_submits = 0;
    module->gridpool_share_accepted = 0;
    module->gridpool_share_duplicate = 0;
    module->gridpool_share_rejected = 0;
    module->gridpool_share_skipped = 0;
    module->gridpool_share_http_status = 0;
    module->gridpool_share_last_submit_time = 0;
    module->gridpool_share_submit_ms = 0;
    module->gridpool_share_last_difficulty = 0;
    module->gridpool_block_submits = 0;
    module->gridpool_block_accepted = 0;
    module->gridpool_block_rejected = 0;
    module->gridpool_block_http_status = 0;
    module->gridpool_block_last_submit_time = 0;
    module->gridpool_block_submit_ms = 0;
    strcpy(module->gridpool_current_state_id, "");
    strcpy(module->gridpool_candidate_state_id, "");
    strcpy(module->gridpool_current_tip_hash, "");
    strcpy(module->gridpool_minimum_difficulty_to_enter_display, "--");
    strcpy(module->gridpool_best_on_deck_difficulty_display, "--");
    strcpy(module->gridpool_current_on_deck_floor_difficulty_display, "--");
    strcpy(module->gridpool_team_hashrate_display, "--");
    strcpy(module->gridpool_template_status, "");
    strcpy(module->gridpool_template_last_error, "");
    strcpy(module->gridpool_template_prev_hash, "");
    strcpy(module->gridpool_share_status, "");
    strcpy(module->gridpool_share_last_error, "");
    strcpy(module->gridpool_share_block_hash, "");
    strcpy(module->gridpool_block_status, "");
    strcpy(module->gridpool_block_last_error, "");
}

static bool append_path(const char *base_url, const char *path, char *dest, size_t dest_len)
{
    if (!base_url || !base_url[0]) {
        return false;
    }

    size_t base_len = strlen(base_url);
    while (base_len > 0 && base_url[base_len - 1] == '/') {
        base_len--;
    }

    int written = snprintf(dest, dest_len, "%.*s%s", (int)base_len, base_url, path);
    return written > 0 && (size_t)written < dest_len;
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    gridpool_http_response_t *response = (gridpool_http_response_t *)evt->user_data;

    if (response == NULL) {
        return ESP_OK;
    }

    if (evt->event_id == HTTP_EVENT_ON_HEADER &&
        evt->header_key && evt->header_value &&
        strcasecmp(evt->header_key, "Content-Length") == 0) {
        response->content_length = atoi(evt->header_value);
        return ESP_OK;
    }

    if (evt->event_id != HTTP_EVENT_ON_DATA || evt->data_len <= 0) {
        return ESP_OK;
    }

    if (response->overflow) {
        return ESP_FAIL;
    }

    if (response->len + evt->data_len + 1 > response->max_bytes) {
        response->overflow = true;
        return ESP_FAIL;
    }

    int required = response->len + evt->data_len + 1;
    if (required > response->capacity) {
        int next_capacity = response->capacity;
        while (next_capacity < required) {
            next_capacity *= 2;
        }
        if (next_capacity > response->max_bytes) {
            next_capacity = response->max_bytes;
        }

        char *next = response_realloc(response->data, next_capacity);
        if (!next) {
            response->overflow = true;
            return ESP_ERR_NO_MEM;
        }
        response->data = next;
        response->capacity = next_capacity;
    }

    memcpy(response->data + response->len, evt->data, evt->data_len);
    response->len += evt->data_len;
    response->data[response->len] = '\0';
    return ESP_OK;
}

static esp_err_t fetch_json_timed(const char *url, char **body, int *http_status, int64_t *elapsed_ms)
{
    *body = NULL;
    *http_status = 0;
    if (elapsed_ms) {
        *elapsed_ms = 0;
    }

    gridpool_http_response_t response = {
        .data = response_malloc(GRIDPOOL_INITIAL_RESPONSE_BYTES),
        .len = 0,
        .capacity = GRIDPOOL_INITIAL_RESPONSE_BYTES,
        .max_bytes = GRIDPOOL_MAX_RESPONSE_BYTES,
        .content_length = 0,
        .overflow = false,
    };
    if (!response.data) {
        return ESP_ERR_NO_MEM;
    }
    response.data[0] = '\0';

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = GRIDPOOL_HTTP_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .user_data = &response,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
    };

    if (strncmp(url, "https://", strlen("https://")) == 0) {
        config.crt_bundle_attach = esp_crt_bundle_attach;
    }

    esp_err_t lock_err = take_gridpool_http_mutex();
    if (lock_err != ESP_OK) {
        free(response.data);
        return lock_err;
    }

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        give_gridpool_http_mutex();
        free(response.data);
        return ESP_FAIL;
    }

    int64_t start_us = esp_timer_get_time();
    esp_err_t err = esp_http_client_perform(client);
    if (elapsed_ms) {
        *elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
    }
    *http_status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    give_gridpool_http_mutex();

    if (err != ESP_OK) {
        free(response.data);
        return err;
    }
    if (response.overflow) {
        free(response.data);
        return ESP_ERR_NO_MEM;
    }
    if (*http_status < 200 || *http_status >= 300) {
        free(response.data);
        return ESP_FAIL;
    }

    *body = response.data;
    return ESP_OK;
}

static esp_err_t fetch_json(const char *url, char **body, int *http_status)
{
    return fetch_json_timed(url, body, http_status, NULL);
}

static esp_err_t post_json_timed(const char *url,
                                 const char *payload,
                                 char **body,
                                 int *http_status,
                                 int64_t *elapsed_ms)
{
    *body = NULL;
    *http_status = 0;
    if (elapsed_ms) {
        *elapsed_ms = 0;
    }

    gridpool_http_response_t response = {
        .data = response_malloc(GRIDPOOL_INITIAL_RESPONSE_BYTES),
        .len = 0,
        .capacity = GRIDPOOL_INITIAL_RESPONSE_BYTES,
        .max_bytes = GRIDPOOL_SHARE_RESPONSE_BYTES,
        .content_length = 0,
        .overflow = false,
    };
    if (!response.data) {
        return ESP_ERR_NO_MEM;
    }
    response.data[0] = '\0';

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = GRIDPOOL_HTTP_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .user_data = &response,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
    };

    if (strncmp(url, "https://", strlen("https://")) == 0) {
        config.crt_bundle_attach = esp_crt_bundle_attach;
    }

    esp_err_t lock_err = take_gridpool_http_mutex();
    if (lock_err != ESP_OK) {
        free(response.data);
        return lock_err;
    }

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        give_gridpool_http_mutex();
        free(response.data);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload, strlen(payload));

    int64_t start_us = esp_timer_get_time();
    esp_err_t err = esp_http_client_perform(client);
    if (elapsed_ms) {
        *elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
    }
    *http_status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    give_gridpool_http_mutex();

    if (err != ESP_OK) {
        free(response.data);
        return err;
    }
    if (response.overflow) {
        free(response.data);
        return ESP_ERR_NO_MEM;
    }

    *body = response.data;
    return ESP_OK;
}

static esp_err_t fetch_rpc_template(const char *url,
                                    const char *username,
                                    const char *password,
                                    const char *payload,
                                    char **body,
                                    int *body_len,
                                    int *http_status,
                                    int *content_length,
                                    bool *overflow,
                                    int64_t *elapsed_ms)
{
    *body = NULL;
    *body_len = 0;
    *http_status = 0;
    *content_length = 0;
    *overflow = false;
    *elapsed_ms = 0;

    gridpool_http_response_t response = {
        .data = response_malloc(GRIDPOOL_INITIAL_RESPONSE_BYTES),
        .len = 0,
        .capacity = GRIDPOOL_INITIAL_RESPONSE_BYTES,
        .max_bytes = GRIDPOOL_MAX_TEMPLATE_RESPONSE_BYTES,
        .content_length = 0,
        .overflow = false,
    };
    if (!response.data) {
        return ESP_ERR_NO_MEM;
    }
    response.data[0] = '\0';

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = GRIDPOOL_RPC_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .user_data = &response,
        .buffer_size = 4096,
        .buffer_size_tx = 1024,
        .keep_alive_enable = false,
    };

    if (username && username[0]) {
        config.username = username;
        config.password = password ? password : "";
        config.auth_type = HTTP_AUTH_TYPE_BASIC;
    }

    if (strncmp(url, "https://", strlen("https://")) == 0) {
        config.crt_bundle_attach = esp_crt_bundle_attach;
    }

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(response.data);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload, strlen(payload));

    int64_t start_us = esp_timer_get_time();
    esp_err_t err = esp_http_client_perform(client);
    *elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
    *http_status = esp_http_client_get_status_code(client);
    *content_length = response.content_length;
    *overflow = response.overflow;
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        free(response.data);
        return err;
    }
    if (response.overflow) {
        free(response.data);
        return ESP_ERR_NO_MEM;
    }
    if (*http_status < 200 || *http_status >= 300) {
        free(response.data);
        return ESP_FAIL;
    }

    *body = response.data;
    *body_len = response.len;
    return ESP_OK;
}

static void copy_json_string(cJSON *object, const char *key, char *dest, size_t dest_len)
{
    cJSON *item = cJSON_GetObjectItem(object, key);
    if (cJSON_IsString(item) && item->valuestring) {
        snprintf(dest, dest_len, "%s", item->valuestring);
    }
}

static double json_number_or_zero(cJSON *object, const char *key)
{
    cJSON *item = cJSON_GetObjectItem(object, key);
    return cJSON_IsNumber(item) ? item->valuedouble : 0;
}

static int json_int_or_zero(cJSON *object, const char *key)
{
    cJSON *item = cJSON_GetObjectItem(object, key);
    return cJSON_IsNumber(item) ? item->valueint : 0;
}

static bool gridpool_miner_value_matches(const char *candidate, const char *miner_address)
{
    if (!candidate || !miner_address || !miner_address[0]) {
        return false;
    }
    size_t miner_len = strlen(miner_address);
    if (strncmp(candidate, miner_address, miner_len) != 0) {
        return false;
    }
    return candidate[miner_len] == '\0' || candidate[miner_len] == '.';
}

static const char *find_json_value(const char *json, const char *key)
{
    char pattern[64];
    int written = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (written <= 0 || (size_t)written >= sizeof(pattern)) {
        return NULL;
    }

    const char *match = strstr(json, pattern);
    if (!match) {
        return NULL;
    }

    const char *colon = strchr(match + written, ':');
    if (!colon) {
        return NULL;
    }

    const char *value = colon + 1;
    while (*value && isspace((unsigned char)*value)) {
        value++;
    }
    return value;
}

static uint64_t scan_json_u64(const char *json, const char *key)
{
    const char *value = find_json_value(json, key);
    if (!value) {
        return 0;
    }
    return strtoull(value, NULL, 10);
}

static double scan_json_double(const char *json, const char *key)
{
    const char *value = find_json_value(json, key);
    if (!value) {
        return 0;
    }
    return strtod(value, NULL);
}

static int scan_json_int(const char *json, const char *key)
{
    return (int)scan_json_double(json, key);
}

static void scan_json_string(const char *json, const char *key, char *dest, size_t dest_len)
{
    if (dest_len == 0) {
        return;
    }
    dest[0] = '\0';

    const char *value = find_json_value(json, key);
    if (!value || *value != '"') {
        return;
    }

    const char *src = value + 1;
    char *out = dest;
    size_t remaining = dest_len - 1;
    bool escaped = false;

    while (*src && remaining > 0) {
        if (escaped) {
            *out++ = *src++;
            remaining--;
            escaped = false;
            continue;
        }
        if (*src == '\\') {
            escaped = true;
            src++;
            continue;
        }
        if (*src == '"') {
            break;
        }
        *out++ = *src++;
        remaining--;
    }
    *out = '\0';
}

static int count_json_array_items(const char *json, const char *key)
{
    const char *value = find_json_value(json, key);
    if (!value || *value != '[') {
        return 0;
    }

    int count = 0;
    int object_depth = 0;
    int array_depth = 1;
    bool in_string = false;
    bool escaped = false;
    bool saw_scalar_item = false;

    for (const char *p = value + 1; *p && array_depth > 0; p++) {
        char c = *p;

        if (escaped) {
            escaped = false;
            continue;
        }
        if (in_string) {
            if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
            if (array_depth == 1 && object_depth == 0) {
                saw_scalar_item = true;
            }
            continue;
        }

        if (c == '[') {
            array_depth++;
        } else if (c == ']') {
            array_depth--;
        } else if (c == '{') {
            if (array_depth == 1 && object_depth == 0) {
                count++;
            }
            object_depth++;
        } else if (c == '}') {
            if (object_depth > 0) {
                object_depth--;
            }
        } else if (array_depth == 1 && object_depth == 0 && c != ',' && !isspace((unsigned char)c)) {
            saw_scalar_item = true;
        }
    }

    return count > 0 ? count : (saw_scalar_item ? 1 : 0);
}

static esp_err_t parse_payouts(SystemModule *module, const char *json, const char *miner_address)
{
    if (!json || !strstr(json, "\"payouts\"") || !strstr(json, "\"network\"")) {
        return ESP_FAIL;
    }

    module->gridpool_payout_sequence = scan_json_u64(json, "sequence");
    module->gridpool_payout_count = count_json_array_items(json, "payouts");
    module->gridpool_coinbase_output_count = count_json_array_items(json, "coinbaseOutputs");
    module->gridpool_current_round = scan_json_int(json, "currentRoundNumber");
    module->gridpool_shared_winner_slot_count = scan_json_int(json, "sharedWinnerSlotCount");
    module->gridpool_on_deck_count = scan_json_int(json, "onDeckCount");
    module->gridpool_current_tip_height = (int64_t)scan_json_u64(json, "currentTipBlockHeight");
    scan_json_string(json, "currentStateId", module->gridpool_current_state_id, sizeof(module->gridpool_current_state_id));
    scan_json_string(json, "candidateStateId", module->gridpool_candidate_state_id, sizeof(module->gridpool_candidate_state_id));
    scan_json_string(json, "currentTipBlockHash", module->gridpool_current_tip_hash, sizeof(module->gridpool_current_tip_hash));
    scan_json_string(json, "currentRoundObservedHashrateDisplay", module->gridpool_team_hashrate_display, sizeof(module->gridpool_team_hashrate_display));

    module->gridpool_winner_slots = 0;
    module->gridpool_winner_value_sats = 0;
    if (miner_address && miner_address[0]) {
        cJSON *root = cJSON_Parse(json);
        cJSON *payouts = root ? cJSON_GetObjectItem(root, "payouts") : NULL;
        if (cJSON_IsArray(payouts)) {
            cJSON *item = NULL;
            cJSON_ArrayForEach(item, payouts) {
                const cJSON *address = cJSON_GetObjectItem(item, "address");
                const cJSON *username = cJSON_GetObjectItem(item, "username");
                const cJSON *value = cJSON_GetObjectItem(item, "value");
                bool matches = (cJSON_IsString(address) && gridpool_miner_value_matches(address->valuestring, miner_address)) ||
                               (cJSON_IsString(username) && gridpool_miner_value_matches(username->valuestring, miner_address));
                if (matches) {
                    module->gridpool_winner_slots++;
                    if (cJSON_IsNumber(value) && value->valuedouble > 0) {
                        module->gridpool_winner_value_sats += (uint64_t)value->valuedouble;
                    }
                }
            }
        }
        cJSON_Delete(root);
    }
    return ESP_OK;
}

static esp_err_t parse_share_advice(SystemModule *module, const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return ESP_FAIL;
    }

    module->gridpool_advice_sequence = (uint64_t)json_number_or_zero(root, "sequence");
    module->gridpool_current_round = json_int_or_zero(root, "currentRoundNumber");
    module->gridpool_shared_winner_slot_count = json_int_or_zero(root, "sharedWinnerSlotCount");
    module->gridpool_on_deck_count = json_int_or_zero(root, "onDeckCount");
    module->gridpool_open_on_deck_slots = json_int_or_zero(root, "openOnDeckSlots");
    module->gridpool_current_tip_height = (int64_t)json_number_or_zero(root, "currentTipBlockHeight");
    module->gridpool_minimum_difficulty_to_enter = json_number_or_zero(root, "minimumDifficultyToEnterOnDeck");
    module->gridpool_best_on_deck_difficulty = json_number_or_zero(root, "bestOnDeckDifficulty");
    module->gridpool_current_on_deck_floor_difficulty = json_number_or_zero(root, "currentOnDeckFloorDifficulty");
    copy_json_string(root, "currentStateId", module->gridpool_current_state_id, sizeof(module->gridpool_current_state_id));
    copy_json_string(root, "candidateStateId", module->gridpool_candidate_state_id, sizeof(module->gridpool_candidate_state_id));
    copy_json_string(root, "currentTipBlockHash", module->gridpool_current_tip_hash, sizeof(module->gridpool_current_tip_hash));
    copy_json_string(root, "minimumDifficultyToEnterOnDeckDisplay", module->gridpool_minimum_difficulty_to_enter_display, sizeof(module->gridpool_minimum_difficulty_to_enter_display));
    copy_json_string(root, "bestOnDeckDifficultyDisplay", module->gridpool_best_on_deck_difficulty_display, sizeof(module->gridpool_best_on_deck_difficulty_display));
    copy_json_string(root, "currentOnDeckFloorDifficultyDisplay", module->gridpool_current_on_deck_floor_difficulty_display, sizeof(module->gridpool_current_on_deck_floor_difficulty_display));

    if (module->gridpool_minimum_difficulty_to_enter_display[0] == '\0') {
        snprintf(module->gridpool_minimum_difficulty_to_enter_display,
                 sizeof(module->gridpool_minimum_difficulty_to_enter_display),
                 "%.0f",
                 module->gridpool_minimum_difficulty_to_enter);
    }
    if (module->gridpool_best_on_deck_difficulty_display[0] == '\0') {
        snprintf(module->gridpool_best_on_deck_difficulty_display,
                 sizeof(module->gridpool_best_on_deck_difficulty_display),
                 "%.0f",
                 module->gridpool_best_on_deck_difficulty);
    }

    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t update_payout_cache(const char *json)
{
    if (!json || !payout_cache_mutex) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!payout_cache_outputs) {
        payout_cache_outputs = response_malloc(sizeof(gridpool_direct_payout_output_t) * GRIDPOOL_TEMPLATE_MAX_PAYOUT_OUTPUTS);
        if (!payout_cache_outputs) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (xSemaphoreTake(payout_cache_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    size_t output_count = 0;
    uint64_t sequence = 0;
    esp_err_t err = gridpool_direct_parse_payout_outputs_json(json,
                                                              payout_cache_outputs,
                                                              GRIDPOOL_TEMPLATE_MAX_PAYOUT_OUTPUTS,
                                                              &output_count,
                                                              &sequence);
    if (err == ESP_OK) {
        payout_cache_output_count = output_count;
        payout_cache_sequence = sequence;
        payout_cache_response_bytes = strlen(json);
        payout_cache_valid = true;
    } else {
        payout_cache_valid = false;
    }

    xSemaphoreGive(payout_cache_mutex);
    return err;
}

static esp_err_t snapshot_payout_cache(gridpool_direct_payout_output_t *outputs,
                                       size_t max_outputs,
                                       size_t *output_count,
                                       uint64_t *sequence,
                                       uint32_t *response_bytes)
{
    if (!outputs || !output_count || !sequence || !response_bytes || !payout_cache_mutex) {
        return ESP_ERR_INVALID_ARG;
    }

    *output_count = 0;
    *sequence = 0;
    *response_bytes = 0;

    if (xSemaphoreTake(payout_cache_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (!payout_cache_valid || payout_cache_output_count > max_outputs) {
        xSemaphoreGive(payout_cache_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(outputs, payout_cache_outputs, payout_cache_output_count * sizeof(gridpool_direct_payout_output_t));
    *output_count = payout_cache_output_count;
    *sequence = payout_cache_sequence;
    *response_bytes = payout_cache_response_bytes;

    xSemaphoreGive(payout_cache_mutex);
    return ESP_OK;
}

static bool get_payout_cache_sequence(uint64_t *sequence)
{
    if (!sequence || !payout_cache_mutex) {
        return false;
    }

    if (xSemaphoreTake(payout_cache_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    bool valid = payout_cache_valid;
    *sequence = valid ? payout_cache_sequence : 0;
    xSemaphoreGive(payout_cache_mutex);
    return valid;
}

static bool choose_slot0_address(const char *configured_payout, const char *stratum_user, char *dest, size_t dest_len)
{
    if (!dest || dest_len == 0) {
        return false;
    }
    dest[0] = '\0';

    const char *source = configured_payout && configured_payout[0] ? configured_payout : stratum_user;
    if (!source || !source[0]) {
        return false;
    }

    size_t len = strcspn(source, ".");
    if (len == 0 || len >= dest_len) {
        return false;
    }

    memcpy(dest, source, len);
    dest[len] = '\0';
    return true;
}

static void record_template_heap_after(SystemModule *module)
{
    module->gridpool_template_heap_after = esp_get_free_heap_size();
    module->gridpool_template_internal_heap_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    module->gridpool_template_spiram_heap_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

static esp_err_t refresh_gridpool_status(GlobalState *GLOBAL_STATE, const char *boot_url);

static void gridpool_direct_template_context_free(gridpool_direct_template_context_t *ctx)
{
    if (!ctx) {
        return;
    }
    gridpool_direct_template_free(&ctx->template_data);
    free(ctx->merkle_path);
    free(ctx->outputs);
    memset(ctx, 0, sizeof(*ctx));
}

static void gridpool_direct_work_context_free(void *source_data)
{
    gridpool_direct_work_context_t *ctx = (gridpool_direct_work_context_t *)source_data;
    if (!ctx) {
        return;
    }
    gridpool_direct_job_free(&ctx->job);
    gridpool_direct_tx_bundle_release(ctx->tx_bundle);
    free(ctx);
}

static double gridpool_direct_submit_difficulty(GlobalState *GLOBAL_STATE)
{
    double submit_difficulty = GLOBAL_STATE->SYSTEM_MODULE.gridpool_minimum_difficulty_to_enter;
    uint64_t configured_min = nvs_config_get_u64(NVS_CONFIG_GRIDPOOL_MIN_SUBMIT_DIFFICULTY);

    if (submit_difficulty < 1.0) {
        submit_difficulty = 1.0;
    }
    if ((double)configured_min > submit_difficulty) {
        submit_difficulty = (double)configured_min;
    }
    return submit_difficulty;
}

static esp_err_t refresh_gridpool_direct_template_context(GlobalState *GLOBAL_STATE,
                                                          const char *boot_url,
                                                          const char *rpc_url,
                                                          const char *rpc_auth_mode,
                                                          const char *rpc_username,
                                                          const char *rpc_password,
                                                          const char *miner_address,
                                                          bool split_rewards,
                                                          gridpool_direct_template_context_t *ctx)
{
    static const char *GBT_PAYLOAD =
        "{\"jsonrpc\":\"1.0\",\"id\":\"gridpool-direct-template\","
        "\"method\":\"getblocktemplate\",\"params\":[{\"rules\":[\"segwit\"]}]}";

    SystemModule *module = &GLOBAL_STATE->SYSTEM_MODULE;
    gridpool_direct_template_context_t next = { 0 };
    char *template_body = NULL;
    char parse_stage[64] = "";
    uint8_t zero_coinbase_txid[32] = { 0 };
    uint8_t ignored_merkle_root[32] = { 0 };
    int template_body_len = 0;
    int rpc_http_status = 0;
    int rpc_content_length = 0;
    bool rpc_overflow = false;
    int64_t rpc_ms = 0;
    int64_t total_start_us = esp_timer_get_time();
    esp_err_t err = ESP_OK;

    module->gridpool_template_last_attempt_time = now_seconds();
    module->gridpool_template_heap_before = esp_get_free_heap_size();
    module->gridpool_template_internal_heap_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    module->gridpool_template_spiram_heap_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    set_template_status(module, false, 0, "Refreshing template", "");

    next.split_rewards = split_rewards;
    if (split_rewards) {
        int64_t payout_start_us = esp_timer_get_time();
        err = refresh_gridpool_status(GLOBAL_STATE, boot_url);
        module->gridpool_template_payout_ms = (uint32_t)((esp_timer_get_time() - payout_start_us) / 1000);
        if (err != ESP_OK) {
            if (!nvs_config_get_bool(NVS_CONFIG_GRIDPOOL_FALLBACK_TO_STRATUM)) {
                ESP_LOGW(TAG, "GridPool payout refresh failed; building pure solo fallback work (%s)", esp_err_to_name(err));
                char error[GRIDPOOL_ERROR_LEN];
                snprintf(error, sizeof(error), "%s", module->gridpool_last_error);
                next.split_rewards = false;
                module->gridpool_template_payout_bytes = 0;
                set_status(module, false, module->gridpool_http_status, "Fallback to Solo Yolo", error);
            } else {
                char error[GRIDPOOL_TEMPLATE_ERROR_LEN];
                snprintf(error, sizeof(error), "GridPool payout refresh failed (%s)", esp_err_to_name(err));
                set_template_status(module, false, module->gridpool_http_status, "Payout refresh failed", error);
                goto fail;
            }
        }

        if (next.split_rewards) {
            next.outputs = response_malloc(sizeof(gridpool_direct_payout_output_t) * GRIDPOOL_TEMPLATE_MAX_PAYOUT_OUTPUTS);
            if (!next.outputs) {
                err = ESP_ERR_NO_MEM;
                goto fail;
            }

            err = snapshot_payout_cache(next.outputs,
                                        GRIDPOOL_TEMPLATE_MAX_PAYOUT_OUTPUTS,
                                        &next.output_count,
                                        &next.payout_sequence,
                                        &next.payout_response_bytes);
            module->gridpool_template_payout_bytes = next.payout_response_bytes;
            if (err != ESP_OK) {
                set_template_status(module, false, 0, "Payout cache unavailable", "GridPool payout outputs have not been cached yet");
                goto fail;
            }
        }
    } else {
        module->gridpool_template_payout_ms = 0;
        module->gridpool_template_payout_bytes = 0;
        set_status(module, false, 0, "Solo Yolo", "");
    }

    bool use_basic_auth = !rpc_auth_mode || rpc_auth_mode[0] == '\0' || strcmp(rpc_auth_mode, "basic") == 0;
    err = fetch_rpc_template(rpc_url,
                             use_basic_auth ? rpc_username : NULL,
                             use_basic_auth ? rpc_password : NULL,
                             GBT_PAYLOAD,
                             &template_body,
                             &template_body_len,
                             &rpc_http_status,
                             &rpc_content_length,
                             &rpc_overflow,
                             &rpc_ms);
    module->gridpool_template_rpc_ms = (uint32_t)rpc_ms;
    module->gridpool_template_http_status = rpc_http_status;
    module->gridpool_template_bytes = template_body_len > 0 ? (uint32_t)template_body_len : 0;
    if (err != ESP_OK) {
        char error[GRIDPOOL_TEMPLATE_ERROR_LEN];
        snprintf(error,
                 sizeof(error),
                 "getblocktemplate failed (%s, HTTP %d, %d/%d bytes, overflow %d)",
                 esp_err_to_name(err),
                 rpc_http_status,
                 template_body_len,
                 rpc_content_length,
                 rpc_overflow ? 1 : 0);
        set_template_status(module, false, rpc_http_status, "RPC fetch failed", error);
        goto fail;
    }

    int64_t parse_start_us = esp_timer_get_time();
    err = gridpool_direct_parse_template_json_buffer(template_body,
                                                     &next.template_data,
                                                     parse_stage,
                                                     sizeof(parse_stage));
    module->gridpool_template_parse_ms = (uint32_t)((esp_timer_get_time() - parse_start_us) / 1000);
    if (err == ESP_OK || strcmp(parse_stage, "transactions txids") == 0 || strcmp(parse_stage, "ok") == 0) {
        template_body = NULL;
    } else {
        free(template_body);
        template_body = NULL;
    }
    if (err != ESP_OK) {
        char error[GRIDPOOL_TEMPLATE_ERROR_LEN];
        snprintf(error,
                 sizeof(error),
                 "Template parse failed at %s (%s, %d/%d bytes)",
                 parse_stage[0] ? parse_stage : "unknown",
                 esp_err_to_name(err),
                 template_body_len,
                 rpc_content_length);
        set_template_status(module, false, rpc_http_status, "Parse failed", error);
        goto fail;
    }

    uint32_t coinbase_weight = 0;
    size_t coinbase_full_len = 0;
    err = gridpool_direct_estimate_coinbase_weight(&next.template_data,
                                                   miner_address,
                                                   next.outputs,
                                                   next.output_count,
                                                   8,
                                                   GRIDPOOL_TEMPLATE_TAG,
                                                   &coinbase_weight,
                                                   &coinbase_full_len);
    if (err != ESP_OK) {
        char error[GRIDPOOL_TEMPLATE_ERROR_LEN];
        snprintf(error, sizeof(error), "Coinbase weight estimate failed (%s)", esp_err_to_name(err));
        set_template_status(module, false, rpc_http_status, "Build failed", error);
        goto fail;
    }

    err = gridpool_direct_trim_template_to_weight(&next.template_data, coinbase_weight);
    if (err != ESP_OK) {
        char error[GRIDPOOL_TEMPLATE_ERROR_LEN];
        snprintf(error, sizeof(error), "Template trim failed (%s)", esp_err_to_name(err));
        set_template_status(module, false, rpc_http_status, "Build failed", error);
        goto fail;
    }

    int64_t build_start_us = esp_timer_get_time();
    err = gridpool_direct_compute_merkle_path(zero_coinbase_txid,
                                              next.template_data.txid_hashes,
                                              next.template_data.tx_count,
                                              &next.merkle_path,
                                              &next.merkle_path_count,
                                              ignored_merkle_root);
    module->gridpool_template_build_ms = (uint32_t)((esp_timer_get_time() - build_start_us) / 1000);
    if (err != ESP_OK) {
        char error[GRIDPOOL_TEMPLATE_ERROR_LEN];
        snprintf(error, sizeof(error), "Merkle path build failed (%s)", esp_err_to_name(err));
        set_template_status(module, false, rpc_http_status, "Build failed", error);
        goto fail;
    }

    next.fetched_us = esp_timer_get_time();
    next.refresh_generation = gridpool_direct_get_refresh_generation();
    next.valid = true;

    gridpool_direct_template_context_free(ctx);
    *ctx = next;
    memset(&next, 0, sizeof(next));

    module->gridpool_template_runs++;
    module->gridpool_template_last_success_time = now_seconds();
    module->gridpool_template_height = ctx->template_data.height;
    module->gridpool_template_tx_count = ctx->template_data.tx_count;
    module->gridpool_template_merkle_path_count = ctx->merkle_path_count;
    module->gridpool_template_coinbase_bytes = (uint32_t)coinbase_full_len;
    module->gridpool_template_gridpool_outputs = ctx->output_count;
    module->gridpool_template_total_ms = (uint32_t)((esp_timer_get_time() - total_start_us) / 1000);
    snprintf(module->gridpool_template_prev_hash,
             sizeof(module->gridpool_template_prev_hash),
             "%s",
             ctx->template_data.previous_block_hash);
    module->gridpool_payout_sequence = ctx->payout_sequence;
    GLOBAL_STATE->block_height = ctx->template_data.height;
    GLOBAL_STATE->network_nonce_diff = (uint64_t)networkDifficulty(ctx->template_data.nbits);
    suffixString(GLOBAL_STATE->network_nonce_diff, GLOBAL_STATE->network_diff_string, DIFF_STRING_SIZE, 0);
    SYSTEM_notify_new_ntime(GLOBAL_STATE, ctx->template_data.curtime);
    set_template_status(module,
                        true,
                        rpc_http_status,
                        ctx->split_rewards ? "Template cached" : "Solo template cached",
                        "");
    record_template_heap_after(module);
    return ESP_OK;

fail:
    module->gridpool_template_failures++;
    free(template_body);
    gridpool_direct_template_context_free(&next);
    record_template_heap_after(module);
    return err;
}

static esp_err_t refresh_gridpool_direct_payout_context(GlobalState *GLOBAL_STATE,
                                                        const char *boot_url,
                                                        gridpool_direct_template_context_t *ctx)
{
    if (!GLOBAL_STATE || !boot_url || !ctx || !ctx->valid) {
        return ESP_ERR_INVALID_ARG;
    }

    SystemModule *module = &GLOBAL_STATE->SYSTEM_MODULE;
    int64_t payout_start_us = esp_timer_get_time();
    esp_err_t err = refresh_gridpool_status(GLOBAL_STATE, boot_url);
    module->gridpool_template_payout_ms = (uint32_t)((esp_timer_get_time() - payout_start_us) / 1000);
    if (err != ESP_OK) {
        return err;
    }

    if (module->gridpool_current_tip_hash[0] != '\0' &&
        strcmp(module->gridpool_current_tip_hash, ctx->template_data.previous_block_hash) != 0) {
        return ESP_ERR_INVALID_STATE;
    }

    gridpool_direct_payout_output_t *outputs =
        response_malloc(sizeof(gridpool_direct_payout_output_t) * GRIDPOOL_TEMPLATE_MAX_PAYOUT_OUTPUTS);
    if (!outputs) {
        return ESP_ERR_NO_MEM;
    }

    size_t output_count = 0;
    uint64_t payout_sequence = 0;
    uint32_t payout_response_bytes = 0;
    err = snapshot_payout_cache(outputs,
                                GRIDPOOL_TEMPLATE_MAX_PAYOUT_OUTPUTS,
                                &output_count,
                                &payout_sequence,
                                &payout_response_bytes);
    if (err != ESP_OK) {
        free(outputs);
        return err;
    }

    free(ctx->outputs);
    ctx->outputs = outputs;
    ctx->output_count = output_count;
    ctx->payout_sequence = payout_sequence;
    ctx->payout_response_bytes = payout_response_bytes;
    ctx->refresh_generation = gridpool_direct_get_refresh_generation();
    ctx->fetched_us = esp_timer_get_time();

    module->gridpool_template_payout_bytes = payout_response_bytes;
    module->gridpool_template_gridpool_outputs = output_count;
    module->gridpool_payout_sequence = payout_sequence;
    set_template_status(module, true, 200, "Payouts refreshed", "");
    return ESP_OK;
}

static bm_job *gridpool_direct_create_bm_job(GlobalState *GLOBAL_STATE,
                                             const gridpool_direct_template_context_t *template_ctx,
                                             const char *miner_address,
                                             const char *username,
                                             uint64_t extranonce_counter,
                                             double submit_difficulty,
                                             uint32_t *build_ms)
{
    if (!template_ctx || !template_ctx->valid || !miner_address || !miner_address[0]) {
        return NULL;
    }

    gridpool_direct_work_context_t *work_ctx = response_malloc(sizeof(*work_ctx));
    if (!work_ctx) {
        return NULL;
    }
    memset(work_ctx, 0, sizeof(*work_ctx));

    uint64_t nonce_material = extranonce_counter ^ (uint64_t)esp_timer_get_time() ^ ((uint64_t)esp_random() << 32);
    uint8_t extranonce[8] = {
        (uint8_t)(nonce_material & 0xff),
        (uint8_t)((nonce_material >> 8) & 0xff),
        (uint8_t)((nonce_material >> 16) & 0xff),
        (uint8_t)((nonce_material >> 24) & 0xff),
        (uint8_t)((nonce_material >> 32) & 0xff),
        (uint8_t)((nonce_material >> 40) & 0xff),
        (uint8_t)((nonce_material >> 48) & 0xff),
        (uint8_t)((nonce_material >> 56) & 0xff),
    };

    int64_t build_start_us = esp_timer_get_time();
    esp_err_t err = gridpool_direct_build_job_from_path(&template_ctx->template_data,
                                                        miner_address,
                                                        template_ctx->outputs,
                                                        template_ctx->output_count,
                                                        template_ctx->merkle_path,
                                                        template_ctx->merkle_path_count,
                                                        extranonce,
                                                        sizeof(extranonce),
                                                        GRIDPOOL_TEMPLATE_TAG,
                                                        submit_difficulty,
                                                        &work_ctx->job);
    if (build_ms) {
        *build_ms = (uint32_t)((esp_timer_get_time() - build_start_us) / 1000);
    }
    if (err != ESP_OK) {
        gridpool_direct_work_context_free(work_ctx);
        return NULL;
    }

    snprintf(work_ctx->miner_address, sizeof(work_ctx->miner_address), "%s", miner_address);
    snprintf(work_ctx->username, sizeof(work_ctx->username), "%s", username && username[0] ? username : miner_address);
    work_ctx->tx_bundle = template_ctx->template_data.tx_bundle;
    gridpool_direct_tx_bundle_retain(work_ctx->tx_bundle);
    work_ctx->payout_sequence = template_ctx->payout_sequence;
    work_ctx->refresh_generation = template_ctx->refresh_generation;
    work_ctx->submit_difficulty = submit_difficulty;
    work_ctx->split_rewards = template_ctx->split_rewards;
    work_ctx->submit_shares = template_ctx->split_rewards;

    GLOBAL_STATE->block_height = template_ctx->template_data.height;
    snprintf(GLOBAL_STATE->scriptsig, sizeof(GLOBAL_STATE->scriptsig), "%s", GRIDPOOL_TEMPLATE_TAG);
    memset(GLOBAL_STATE->coinbase_outputs, 0, sizeof(GLOBAL_STATE->coinbase_outputs));
    GLOBAL_STATE->coinbase_output_count = 0;
    GLOBAL_STATE->coinbase_value_total_satoshis = template_ctx->template_data.coinbase_value;
    GLOBAL_STATE->coinbase_value_user_satoshis = work_ctx->job.slot0_value_sats;
    if (MAX_COINBASE_TX_OUTPUTS > 0) {
        GLOBAL_STATE->coinbase_outputs[0].value_satoshis = work_ctx->job.slot0_value_sats;
        snprintf(GLOBAL_STATE->coinbase_outputs[0].address,
                 sizeof(GLOBAL_STATE->coinbase_outputs[0].address),
                 "%s",
                 miner_address);
        GLOBAL_STATE->coinbase_outputs[0].is_user_output = true;
        GLOBAL_STATE->coinbase_output_count = 1;
    }
    int display_limit = MAX_COINBASE_TX_OUTPUTS;
    for (size_t i = 0; i < template_ctx->output_count && GLOBAL_STATE->coinbase_output_count < display_limit; i++) {
        int index = GLOBAL_STATE->coinbase_output_count++;
        GLOBAL_STATE->coinbase_outputs[index].value_satoshis = template_ctx->outputs[i].value_sats;
        snprintf(GLOBAL_STATE->coinbase_outputs[index].address,
                 sizeof(GLOBAL_STATE->coinbase_outputs[index].address),
                 "%s",
                 template_ctx->outputs[i].address);
    }
    if (template_ctx->output_count > (size_t)(display_limit - 1) && GLOBAL_STATE->coinbase_output_count == display_limit) {
        int hidden = (int)template_ctx->output_count - (display_limit - 1);
        snprintf(GLOBAL_STATE->coinbase_outputs[display_limit - 1].address,
                 sizeof(GLOBAL_STATE->coinbase_outputs[display_limit - 1].address),
                 "... %d more GridPool outputs",
                 hidden);
        GLOBAL_STATE->coinbase_outputs[display_limit - 1].value_satoshis = 0;
    }

    bm_job *job = calloc(1, sizeof(*job));
    if (!job) {
        gridpool_direct_work_context_free(work_ctx);
        return NULL;
    }
    memcpy(job, &work_ctx->job.bm_job, sizeof(*job));

    char job_id[24];
    snprintf(job_id, sizeof(job_id), "gp%08" PRIx32, GLOBAL_STATE->SYSTEM_MODULE.gridpool_direct_jobs_sent + 1);
    job->jobid = strdup(job_id);

    char extranonce_hex[sizeof(extranonce) * 2 + 1];
    bin2hex(extranonce, sizeof(extranonce), extranonce_hex, sizeof(extranonce_hex));
    job->extranonce2 = strdup(extranonce_hex);
    if (!job->jobid || !job->extranonce2) {
        free_bm_job(job);
        gridpool_direct_work_context_free(work_ctx);
        return NULL;
    }

    job->pool_diff = submit_difficulty;
    job->source = BM_JOB_SOURCE_GRIDPOOL_DIRECT;
    job->source_data = work_ctx;
    job->source_data_free = gridpool_direct_work_context_free;
    return job;
}

bool gridpool_direct_is_enabled(void)
{
    return nvs_config_get_bool(NVS_CONFIG_GRIDPOOL_ENABLED);
}

static bool gridpool_direct_uses_split_rewards(void)
{
    char *reward_mode = nvs_config_get_string(NVS_CONFIG_GRIDPOOL_REWARD_MODE);
    bool split_rewards = !reward_mode || reward_mode[0] == '\0' ||
                         strcmp(reward_mode, GRIDPOOL_REWARD_MODE_SOLO) != 0;
    free(reward_mode);
    return split_rewards;
}

static void set_share_status(SystemModule *module, int http_status, const char *status, const char *error)
{
    module->gridpool_share_http_status = http_status;
    snprintf(module->gridpool_share_status, sizeof(module->gridpool_share_status), "%s", status ? status : "");
    snprintf(module->gridpool_share_last_error, sizeof(module->gridpool_share_last_error), "%s", error ? error : "");
}

static void set_block_status(SystemModule *module, int http_status, const char *status, const char *error)
{
    module->gridpool_block_http_status = http_status;
    snprintf(module->gridpool_block_status, sizeof(module->gridpool_block_status), "%s", status ? status : "");
    snprintf(module->gridpool_block_last_error, sizeof(module->gridpool_block_last_error), "%s", error ? error : "");
}

static esp_err_t gridpool_direct_serialize_bm_job_header(const bm_job *job,
                                                         uint32_t nonce,
                                                         uint32_t rolled_version,
                                                         uint8_t header[80])
{
    if (!job || !header) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(header, &rolled_version, 4);
    reverse_32bit_words(job->prev_block_hash, header + 4);
    reverse_32bit_words(job->merkle_root, header + 36);
    memcpy(header + 68, &job->ntime, 4);
    memcpy(header + 72, &job->target, 4);
    memcpy(header + 76, &nonce, 4);
    return ESP_OK;
}

static size_t gridpool_direct_encode_varint(uint64_t value, uint8_t out[9])
{
    if (value < 0xfd) {
        out[0] = (uint8_t)value;
        return 1;
    }
    if (value <= 0xffff) {
        out[0] = 0xfd;
        out[1] = (uint8_t)(value & 0xff);
        out[2] = (uint8_t)((value >> 8) & 0xff);
        return 3;
    }
    if (value <= 0xffffffffULL) {
        out[0] = 0xfe;
        for (int i = 0; i < 4; i++) {
            out[1 + i] = (uint8_t)((value >> (i * 8)) & 0xff);
        }
        return 5;
    }
    out[0] = 0xff;
    for (int i = 0; i < 8; i++) {
        out[1 + i] = (uint8_t)((value >> (i * 8)) & 0xff);
    }
    return 9;
}

static esp_err_t http_write_all(esp_http_client_handle_t client, const char *data, size_t len)
{
    size_t written = 0;
    while (written < len) {
        int ret = esp_http_client_write(client, data + written, len - written);
        if (ret <= 0) {
            return ESP_FAIL;
        }
        written += (size_t)ret;
    }
    return ESP_OK;
}

static esp_err_t http_write_hex_bytes(esp_http_client_handle_t client, const uint8_t *data, size_t len)
{
    char hex[513];
    while (len > 0) {
        size_t chunk = len > 256 ? 256 : len;
        if (bin2hex(data, chunk, hex, sizeof(hex)) != chunk * 2) {
            return ESP_FAIL;
        }
        ESP_RETURN_ON_ERROR(http_write_all(client, hex, chunk * 2), TAG, "write hex");
        data += chunk;
        len -= chunk;
    }
    return ESP_OK;
}

static size_t tx_bundle_selected_data_len(const gridpool_direct_tx_bundle_t *bundle)
{
    if (!bundle) {
        return 0;
    }
    size_t total = 0;
    for (size_t i = 0; i < bundle->tx_count; i++) {
        total += bundle->tx_lengths[i];
    }
    return total;
}

esp_err_t gridpool_direct_submit_block(GlobalState *GLOBAL_STATE,
                                       const bm_job *job,
                                       uint32_t nonce,
                                       uint32_t rolled_version)
{
    if (!GLOBAL_STATE || !job || job->source != BM_JOB_SOURCE_GRIDPOOL_DIRECT || !job->source_data) {
        return ESP_ERR_INVALID_ARG;
    }

    gridpool_direct_work_context_t *ctx = (gridpool_direct_work_context_t *)job->source_data;
    if (!ctx->tx_bundle || !ctx->job.coinbase_tx) {
        set_block_status(&GLOBAL_STATE->SYSTEM_MODULE, 0, "Unavailable", "No retained template transactions for submitblock");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t header[80];
    esp_err_t err = gridpool_direct_serialize_bm_job_header(job, nonce, rolled_version, header);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t tx_count_varint[9];
    size_t tx_count_varint_len = gridpool_direct_encode_varint(ctx->tx_bundle->tx_count + 1, tx_count_varint);
    size_t selected_tx_data_len = tx_bundle_selected_data_len(ctx->tx_bundle);
    size_t block_bytes = sizeof(header) + tx_count_varint_len + ctx->job.coinbase_tx_len + selected_tx_data_len;
    if (block_bytes > (SIZE_MAX / 2)) {
        return ESP_ERR_INVALID_SIZE;
    }

    const char *prefix = "{\"jsonrpc\":\"1.0\",\"id\":\"gridpool-submitblock\",\"method\":\"submitblock\",\"params\":[\"";
    const char *suffix = "\"]}";
    size_t content_len = strlen(prefix) + block_bytes * 2 + strlen(suffix);
    if (content_len > INT_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    char *rpc_url = nvs_config_get_string(NVS_CONFIG_GRIDPOOL_BITCOIN_RPC_URL);
    char *rpc_auth_mode = nvs_config_get_string(NVS_CONFIG_GRIDPOOL_BITCOIN_RPC_AUTH_MODE);
    char *rpc_username = nvs_config_get_string(NVS_CONFIG_GRIDPOOL_BITCOIN_RPC_USERNAME);
    char *rpc_password = nvs_config_get_string(NVS_CONFIG_GRIDPOOL_BITCOIN_RPC_PASSWORD);
    bool use_basic_auth = !rpc_auth_mode || rpc_auth_mode[0] == '\0' || strcmp(rpc_auth_mode, "basic") == 0;

    if (!rpc_url || !rpc_url[0] || (use_basic_auth && (!rpc_username || !rpc_username[0]))) {
        free(rpc_url);
        free(rpc_auth_mode);
        free(rpc_username);
        free(rpc_password);
        set_block_status(&GLOBAL_STATE->SYSTEM_MODULE, 0, "RPC not configured", "Bitcoin RPC submitblock is not configured");
        return ESP_ERR_INVALID_STATE;
    }

    esp_http_client_config_t config = {
        .url = rpc_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = GRIDPOOL_RPC_TIMEOUT_MS,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
        .keep_alive_enable = false,
    };
    if (use_basic_auth) {
        config.username = rpc_username;
        config.password = rpc_password ? rpc_password : "";
        config.auth_type = HTTP_AUTH_TYPE_BASIC;
    }
    if (strncmp(rpc_url, "https://", strlen("https://")) == 0) {
        config.crt_bundle_attach = esp_crt_bundle_attach;
    }

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(rpc_url);
        free(rpc_auth_mode);
        free(rpc_username);
        free(rpc_password);
        return ESP_FAIL;
    }

    SystemModule *module = &GLOBAL_STATE->SYSTEM_MODULE;
    module->gridpool_block_submits++;
    module->gridpool_block_last_submit_time = now_seconds();
    set_block_status(module, 0, "Submitting", "");

    esp_http_client_set_header(client, "Content-Type", "application/json");
    int64_t start_us = esp_timer_get_time();
    err = esp_http_client_open(client, (int)content_len);
    if (err == ESP_OK) {
        err = http_write_all(client, prefix, strlen(prefix));
    }
    if (err == ESP_OK) {
        err = http_write_hex_bytes(client, header, sizeof(header));
    }
    if (err == ESP_OK) {
        err = http_write_hex_bytes(client, tx_count_varint, tx_count_varint_len);
    }
    if (err == ESP_OK) {
        err = http_write_hex_bytes(client, ctx->job.coinbase_tx, ctx->job.coinbase_tx_len);
    }
    for (size_t i = 0; err == ESP_OK && i < ctx->tx_bundle->tx_count; i++) {
        uint32_t offset = ctx->tx_bundle->tx_offsets[i];
        uint32_t length = ctx->tx_bundle->tx_lengths[i];
        if ((size_t)offset > ctx->tx_bundle->tx_data_len ||
            (size_t)length > ctx->tx_bundle->tx_data_len - offset) {
            err = ESP_ERR_INVALID_SIZE;
            break;
        }
        err = http_write_hex_bytes(client, ctx->tx_bundle->tx_data + offset, length);
    }
    if (err == ESP_OK) {
        err = http_write_all(client, suffix, strlen(suffix));
    }

    int http_status = 0;
    char response[512] = "";
    if (err == ESP_OK) {
        esp_http_client_fetch_headers(client);
        http_status = esp_http_client_get_status_code(client);
        int read_len = esp_http_client_read_response(client, response, sizeof(response) - 1);
        if (read_len >= 0) {
            response[read_len] = '\0';
        }
    }
    module->gridpool_block_submit_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000);
    module->gridpool_block_http_status = http_status;
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    free(rpc_url);
    free(rpc_auth_mode);
    free(rpc_username);
    free(rpc_password);

    if (err != ESP_OK || http_status < 200 || http_status >= 300) {
        char error[GRIDPOOL_BLOCK_ERROR_LEN];
        snprintf(error, sizeof(error), "submitblock failed (%s, HTTP %d)", esp_err_to_name(err), http_status);
        module->gridpool_block_rejected++;
        set_block_status(module, http_status, "Submit failed", error);
        ESP_LOGE(TAG, "%s", error);
        return err != ESP_OK ? err : ESP_FAIL;
    }

    cJSON *reply = cJSON_Parse(response);
    cJSON *result = reply ? cJSON_GetObjectItem(reply, "result") : NULL;
    cJSON *rpc_error = reply ? cJSON_GetObjectItem(reply, "error") : NULL;
    bool accepted = reply && cJSON_IsNull(result) && cJSON_IsNull(rpc_error);
    if (accepted) {
        module->gridpool_block_accepted++;
        set_block_status(module, http_status, "Accepted", "");
        ESP_LOGI(TAG,
                 "submitblock accepted (%zu bytes, %" PRIu32 " ms)",
                 block_bytes,
                 module->gridpool_block_submit_ms);
        cJSON_Delete(reply);
        return ESP_OK;
    }

    char error[GRIDPOOL_BLOCK_ERROR_LEN] = "submitblock rejected";
    if (cJSON_IsString(result)) {
        snprintf(error, sizeof(error), "%s", result->valuestring);
    } else if (rpc_error) {
        char *printed = cJSON_PrintUnformatted(rpc_error);
        if (printed) {
            snprintf(error, sizeof(error), "%s", printed);
            free(printed);
        }
    }
    cJSON_Delete(reply);
    module->gridpool_block_rejected++;
    set_block_status(module, http_status, "Rejected", error);
    ESP_LOGE(TAG, "submitblock rejected: %s", error);
    return ESP_FAIL;
}

esp_err_t gridpool_direct_submit_share(GlobalState *GLOBAL_STATE,
                                       const bm_job *job,
                                       uint32_t nonce,
                                       uint32_t rolled_version,
                                       double difficulty,
                                       int64_t found_timestamp_us)
{
    if (!GLOBAL_STATE || !job || job->source != BM_JOB_SOURCE_GRIDPOOL_DIRECT || !job->source_data) {
        return ESP_ERR_INVALID_ARG;
    }

    SystemModule *module = &GLOBAL_STATE->SYSTEM_MODULE;
    gridpool_direct_work_context_t *ctx = (gridpool_direct_work_context_t *)job->source_data;
    if (!ctx->submit_shares) {
        set_share_status(module, 0, "Solo mode", "Pure solo work is not submitted to GridPool");
        return ESP_ERR_INVALID_STATE;
    }

    if (ctx->refresh_generation != gridpool_direct_get_refresh_generation()) {
        module->gridpool_share_skipped++;
        set_share_status(module, 0, "Skipped", "GridPool work is stale after template refresh request");
        return ESP_ERR_INVALID_STATE;
    }

    uint64_t current_payout_sequence = 0;
    if (get_payout_cache_sequence(&current_payout_sequence) &&
        current_payout_sequence != ctx->payout_sequence) {
        gridpool_direct_request_template_refresh();
        module->gridpool_share_skipped++;
        set_share_status(module, 0, "Skipped", "GridPool payout sequence changed; refreshing work");
        return ESP_ERR_INVALID_STATE;
    }

    int64_t now_us = esp_timer_get_time();
    if (last_gridpool_share_submit_us > 0 &&
        (now_us - last_gridpool_share_submit_us) < (GRIDPOOL_DIRECT_MIN_SHARE_INTERVAL_MS * 1000LL)) {
        module->gridpool_share_skipped++;
        set_share_status(module, 0, "Skipped", "GridPool share submit rate limited");
        return ESP_ERR_INVALID_STATE;
    }
    last_gridpool_share_submit_us = now_us;

    char *boot_url = nvs_config_get_string(NVS_CONFIG_GRIDPOOL_BOOT_URL);
    char url[GRIDPOOL_MAX_URL_BYTES];
    if (!append_path(boot_url, "/api/mining/share", url, sizeof(url))) {
        free(boot_url);
        return ESP_ERR_INVALID_ARG;
    }
    free(boot_url);

    uint8_t header[80];
    esp_err_t err = gridpool_direct_serialize_bm_job_header(job, nonce, rolled_version, header);
    if (err != ESP_OK) {
        return err;
    }

    char header_hex[161];
    if (bin2hex(header, sizeof(header), header_hex, sizeof(header_hex)) != 160) {
        return ESP_FAIL;
    }

    const uint8_t *coinbase_tx = ctx->job.coinbase_legacy_tx ? ctx->job.coinbase_legacy_tx : ctx->job.coinbase_tx;
    size_t coinbase_tx_len = ctx->job.coinbase_legacy_tx ? ctx->job.coinbase_legacy_tx_len : ctx->job.coinbase_tx_len;
    char *coinbase_hex = response_malloc(coinbase_tx_len * 2 + 1);
    if (!coinbase_hex) {
        return ESP_ERR_NO_MEM;
    }
    if (bin2hex(coinbase_tx, coinbase_tx_len, coinbase_hex, coinbase_tx_len * 2 + 1) !=
        coinbase_tx_len * 2) {
        free(coinbase_hex);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *merkle_array = cJSON_CreateArray();
    if (!root || !merkle_array) {
        cJSON_Delete(root);
        cJSON_Delete(merkle_array);
        free(coinbase_hex);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "minerAddress", ctx->miner_address);
    cJSON_AddStringToObject(root, "username", ctx->username);
    cJSON_AddStringToObject(root, "headerHex", header_hex);
    cJSON_AddStringToObject(root, "coinbaseHex", coinbase_hex);
    cJSON_AddItemToObject(root, "merklePath", merkle_array);
    for (size_t i = 0; i < ctx->job.merkle_path_count; i++) {
        char branch_hex[65];
        if (bin2hex(ctx->job.merkle_path + i * 32, 32, branch_hex, sizeof(branch_hex)) == 64) {
            cJSON_AddItemToArray(merkle_array, cJSON_CreateString(branch_hex));
        }
    }
    cJSON_AddStringToObject(root, "prevBlockHash", ctx->job.previous_block_hash);
    cJSON_AddNumberToObject(root, "nonce", (double)nonce);
    cJSON_AddNumberToObject(root, "difficulty", difficulty);

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(coinbase_hex);
    if (!payload) {
        return ESP_ERR_NO_MEM;
    }

    char *response = NULL;
    int http_status = 0;
    int64_t submit_ms = 0;
    err = post_json_timed(url, payload, &response, &http_status, &submit_ms);
    free(payload);

    module->gridpool_share_submits++;
    module->gridpool_share_last_submit_time = now_seconds();
    module->gridpool_share_submit_ms = (uint32_t)submit_ms;
    module->gridpool_share_last_difficulty = difficulty;
    module->gridpool_share_http_status = http_status;

    if (err != ESP_OK || !response) {
        char error[GRIDPOOL_SHARE_ERROR_LEN];
        snprintf(error, sizeof(error), "POST /api/mining/share failed (%s, HTTP %d)", esp_err_to_name(err), http_status);
        module->gridpool_share_rejected++;
        set_share_status(module, http_status, "Submit failed", error);
        SYSTEM_notify_rejected_share(GLOBAL_STATE, error);
        free(response);
        return err;
    }

    cJSON *reply = cJSON_Parse(response);
    const cJSON *status = reply ? cJSON_GetObjectItem(reply, "status") : NULL;
    const cJSON *reason = reply ? cJSON_GetObjectItem(reply, "reason") : NULL;
    const cJSON *computed_difficulty = reply ? cJSON_GetObjectItem(reply, "difficulty") : NULL;
    const cJSON *block_hash = reply ? cJSON_GetObjectItem(reply, "blockHash") : NULL;
    const char *status_text = cJSON_IsString(status) ? status->valuestring : "";
    const char *reason_text = cJSON_IsString(reason) ? reason->valuestring : "";

    if (cJSON_IsNumber(computed_difficulty)) {
        module->gridpool_share_last_difficulty = computed_difficulty->valuedouble;
    }
    if (cJSON_IsString(block_hash)) {
        snprintf(module->gridpool_share_block_hash, sizeof(module->gridpool_share_block_hash), "%s", block_hash->valuestring);
    }

    if (http_status >= 200 && http_status < 300 && strcmp(status_text, "accepted") == 0) {
        module->gridpool_share_accepted++;
        set_share_status(module, http_status, "Accepted", "");
        SYSTEM_notify_accepted_share(GLOBAL_STATE);
        gridpool_direct_request_template_refresh();
    } else if (http_status >= 200 && http_status < 300 && strcmp(status_text, "duplicate") == 0) {
        module->gridpool_share_duplicate++;
        set_share_status(module, http_status, "Duplicate", "");
    } else {
        char error[GRIDPOOL_SHARE_ERROR_LEN];
        snprintf(error,
                 sizeof(error),
                 "%s",
                 reason_text[0] ? reason_text : (status_text[0] ? status_text : "GridPool share rejected"));
        module->gridpool_share_rejected++;
        set_share_status(module, http_status, "Rejected", error);
        SYSTEM_notify_rejected_share(GLOBAL_STATE, error);
        gridpool_direct_request_template_refresh();
        err = ESP_FAIL;
    }

    float process_time = (esp_timer_get_time() - found_timestamp_us) / 1000.0f;
    module->process_time = process_time;
    module->response_time = (float)submit_ms;
    ESP_LOGI(TAG,
             "GridPool share %s: diff %.2f, HTTP %d, submit %.1f ms",
             module->gridpool_share_status,
             module->gridpool_share_last_difficulty,
             http_status,
             (double)submit_ms);

    cJSON_Delete(reply);
    free(response);
    return err;
}

static esp_err_t refresh_gridpool_status(GlobalState *GLOBAL_STATE, const char *boot_url)
{
    SystemModule *module = &GLOBAL_STATE->SYSTEM_MODULE;
    char url[GRIDPOOL_MAX_URL_BYTES];
    char miner_address[GRIDPOOL_DIRECT_ADDRESS_MAX_LEN] = "";
    char *payout_address = nvs_config_get_string(NVS_CONFIG_GRIDPOOL_PAYOUT_ADDRESS);
    char *stratum_user = nvs_config_get_string(NVS_CONFIG_STRATUM_USER);
    char *body = NULL;
    int http_status = 0;

    module->gridpool_last_attempt_time = now_seconds();
    choose_slot0_address(payout_address, stratum_user, miner_address, sizeof(miner_address));
    free(payout_address);
    free(stratum_user);

    if (!append_path(boot_url, "/api/mining/payouts", url, sizeof(url))) {
        set_status(module, false, 0, "Invalid Boot URL", "GridPool Boot URL is empty or too long");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = fetch_json(url, &body, &http_status);
    if (err != ESP_OK) {
        char error[GRIDPOOL_ERROR_LEN];
        snprintf(error, sizeof(error), "GET /api/mining/payouts failed (%s, HTTP %d)",
                 esp_err_to_name(err), http_status);
        set_status(module, false, http_status, "Unreachable", error);
        ESP_LOGW(TAG, "%s", error);
        return err;
    }

    err = parse_payouts(module, body, miner_address);
    if (err == ESP_OK) {
        esp_err_t cache_err = update_payout_cache(body);
        if (cache_err != ESP_OK) {
            ESP_LOGW(TAG, "Could not cache GridPool payout outputs: %s", esp_err_to_name(cache_err));
        }
    }
    free(body);
    body = NULL;
    if (err != ESP_OK) {
        set_status(module, false, http_status, "Parse error", "Could not parse GridPool payout response");
        return err;
    }

    module->gridpool_last_success_time = now_seconds();
    set_status(module, true, http_status, "Reachable", "");

    if (!append_path(boot_url, "/api/mining/share-advice", url, sizeof(url))) {
        return ESP_OK;
    }

    err = fetch_json(url, &body, &http_status);
    if (err != ESP_OK) {
        char error[GRIDPOOL_ERROR_LEN];
        snprintf(error, sizeof(error), "GET /api/mining/share-advice failed (%s, HTTP %d)",
                 esp_err_to_name(err), http_status);
        set_status(module, true, http_status, "Reachable, advice unavailable", error);
        ESP_LOGW(TAG, "%s", error);
        return ESP_OK;
    }

    err = parse_share_advice(module, body);
    free(body);
    if (err != ESP_OK) {
        set_status(module, true, http_status, "Reachable, advice parse error", "Could not parse GridPool share advice");
        return ESP_OK;
    }

    module->gridpool_last_success_time = now_seconds();
    set_status(module, true, http_status, "Reachable", "");
    return ESP_OK;
}

static void gridpool_status_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;

    while (1) {
        bool enabled = nvs_config_get_bool(NVS_CONFIG_GRIDPOOL_ENABLED);
        bool split_rewards = gridpool_direct_uses_split_rewards();
        char *boot_url = nvs_config_get_string(NVS_CONFIG_GRIDPOOL_BOOT_URL);

        if (!enabled) {
            clear_runtime_status(&GLOBAL_STATE->SYSTEM_MODULE);
            set_status(&GLOBAL_STATE->SYSTEM_MODULE, false, 0, "Disabled", "");
            free(boot_url);
            vTaskDelay(pdMS_TO_TICKS(GRIDPOOL_DISABLED_POLL_INTERVAL_MS));
            continue;
        }
        if (!split_rewards) {
            set_status(&GLOBAL_STATE->SYSTEM_MODULE, false, 0, "Solo Yolo", "");
            GLOBAL_STATE->SYSTEM_MODULE.gridpool_last_attempt_time = now_seconds();
            free(boot_url);
            vTaskDelay(pdMS_TO_TICKS(GRIDPOOL_DIRECT_STATUS_POLL_INTERVAL_MS));
            continue;
        }

        esp_err_t err = refresh_gridpool_status(GLOBAL_STATE, boot_url);
        free(boot_url);

        vTaskDelay(pdMS_TO_TICKS(err == ESP_OK ? GRIDPOOL_DIRECT_STATUS_POLL_INTERVAL_MS : GRIDPOOL_ERROR_POLL_INTERVAL_MS));
    }
}

static void gridpool_template_probe_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;
    gridpool_direct_template_context_t template_ctx = { 0 };
    uint64_t extranonce_counter = ((uint64_t)esp_random() << 32) ^ (uint64_t)esp_timer_get_time();

    vTaskDelay(pdMS_TO_TICKS(GRIDPOOL_TEMPLATE_INITIAL_DELAY_MS));

    while (1) {
        bool enabled = nvs_config_get_bool(NVS_CONFIG_GRIDPOOL_ENABLED);
        bool split_rewards = gridpool_direct_uses_split_rewards();
        char *boot_url = nvs_config_get_string(NVS_CONFIG_GRIDPOOL_BOOT_URL);
        char *rpc_url = nvs_config_get_string(NVS_CONFIG_GRIDPOOL_BITCOIN_RPC_URL);
        char *rpc_auth_mode = nvs_config_get_string(NVS_CONFIG_GRIDPOOL_BITCOIN_RPC_AUTH_MODE);
        char *rpc_username = nvs_config_get_string(NVS_CONFIG_GRIDPOOL_BITCOIN_RPC_USERNAME);
        char *rpc_password = nvs_config_get_string(NVS_CONFIG_GRIDPOOL_BITCOIN_RPC_PASSWORD);
        char *payout_address = nvs_config_get_string(NVS_CONFIG_GRIDPOOL_PAYOUT_ADDRESS);
        char *stratum_user = nvs_config_get_string(NVS_CONFIG_STRATUM_USER);

        if (!enabled) {
            gridpool_direct_template_context_free(&template_ctx);
            set_template_status(&GLOBAL_STATE->SYSTEM_MODULE, false, 0, "Disabled", "");
            free(boot_url);
            free(rpc_url);
            free(rpc_auth_mode);
            free(rpc_username);
            free(rpc_password);
            free(payout_address);
            free(stratum_user);
            vTaskDelay(pdMS_TO_TICKS(GRIDPOOL_TEMPLATE_UNCONFIGURED_INTERVAL_MS));
            continue;
        } else if (!rpc_url || !rpc_url[0]) {
            gridpool_direct_template_context_free(&template_ctx);
            set_template_status(&GLOBAL_STATE->SYSTEM_MODULE, false, 0, "RPC not configured", "Bitcoin RPC URL is empty");
            free(boot_url);
            free(rpc_url);
            free(rpc_auth_mode);
            free(rpc_username);
            free(rpc_password);
            free(payout_address);
            free(stratum_user);
            vTaskDelay(pdMS_TO_TICKS(GRIDPOOL_TEMPLATE_UNCONFIGURED_INTERVAL_MS));
            continue;
        }

        bool use_basic_auth = !rpc_auth_mode || rpc_auth_mode[0] == '\0' || strcmp(rpc_auth_mode, "basic") == 0;
        if (use_basic_auth && (!rpc_username || !rpc_username[0])) {
            gridpool_direct_template_context_free(&template_ctx);
            set_template_status(&GLOBAL_STATE->SYSTEM_MODULE, false, 0, "RPC not configured", "Bitcoin RPC username is empty");
            free(boot_url);
            free(rpc_url);
            free(rpc_auth_mode);
            free(rpc_username);
            free(rpc_password);
            free(payout_address);
            free(stratum_user);
            vTaskDelay(pdMS_TO_TICKS(GRIDPOOL_TEMPLATE_UNCONFIGURED_INTERVAL_MS));
            continue;
        }

        char miner_address[GRIDPOOL_DIRECT_ADDRESS_MAX_LEN];
        if (!choose_slot0_address(payout_address, stratum_user, miner_address, sizeof(miner_address))) {
            gridpool_direct_template_context_free(&template_ctx);
            set_template_status(&GLOBAL_STATE->SYSTEM_MODULE, false, 0, "Payout not configured", "GridPool payout address is empty");
            free(boot_url);
            free(rpc_url);
            free(rpc_auth_mode);
            free(rpc_username);
            free(rpc_password);
            free(payout_address);
            free(stratum_user);
            vTaskDelay(pdMS_TO_TICKS(GRIDPOOL_TEMPLATE_UNCONFIGURED_INTERVAL_MS));
            continue;
        }

        esp_err_t err = refresh_gridpool_direct_template_context(GLOBAL_STATE,
                                                                 boot_url,
                                                                 rpc_url,
                                                                 rpc_auth_mode,
                                                                 rpc_username,
                                                                 rpc_password,
                                                                 miner_address,
                                                                 split_rewards,
                                                                 &template_ctx);
        if (err != ESP_OK) {
            if (!template_ctx.valid) {
                free(boot_url);
                free(rpc_url);
                free(rpc_auth_mode);
                free(rpc_username);
                free(rpc_password);
                free(payout_address);
                free(stratum_user);
                vTaskDelay(pdMS_TO_TICKS(GRIDPOOL_TEMPLATE_ERROR_INTERVAL_MS));
                continue;
            }

            ESP_LOGW(TAG,
                     "GridPool template refresh failed; continuing with cached height %" PRIu32 " work: %s",
                     template_ctx.template_data.height,
                     esp_err_to_name(err));
            char cached_error[GRIDPOOL_TEMPLATE_ERROR_LEN];
            snprintf(cached_error,
                     sizeof(cached_error),
                     "%s",
                     GLOBAL_STATE->SYSTEM_MODULE.gridpool_template_last_error);
            set_template_status(&GLOBAL_STATE->SYSTEM_MODULE,
                                true,
                                GLOBAL_STATE->SYSTEM_MODULE.gridpool_template_http_status,
                                template_ctx.split_rewards ? "Mining cached GridPool work" : "Mining cached Solo Yolo work",
                                cached_error);
        }

        int64_t refresh_started_us = esp_timer_get_time();
        while (nvs_config_get_bool(NVS_CONFIG_GRIDPOOL_ENABLED) &&
               template_ctx.valid &&
               ((esp_timer_get_time() - refresh_started_us) / 1000) < GRIDPOOL_DIRECT_TEMPLATE_REFRESH_MS) {
            uint64_t current_payout_sequence = 0;
            if (template_ctx.split_rewards &&
                get_payout_cache_sequence(&current_payout_sequence) &&
                current_payout_sequence != template_ctx.payout_sequence) {
                gridpool_direct_request_template_refresh();
            }

            if (template_ctx.split_rewards &&
                template_ctx.refresh_generation != gridpool_direct_get_refresh_generation()) {
                esp_err_t refresh_err = refresh_gridpool_direct_payout_context(GLOBAL_STATE, boot_url, &template_ctx);
                if (refresh_err == ESP_OK) {
                    refresh_started_us = esp_timer_get_time();
                    continue;
                }
                ESP_LOGW(TAG, "Could not refresh GridPool payouts without new template: %s", esp_err_to_name(refresh_err));
                break;
            }

            if (!GLOBAL_STATE->ASIC_initalized || !GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs) {
                set_template_status(&GLOBAL_STATE->SYSTEM_MODULE, true, 0, "Waiting for ASIC", "");
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }

            double submit_difficulty = gridpool_direct_submit_difficulty(GLOBAL_STATE);
            uint32_t build_ms = 0;
            bm_job *job = gridpool_direct_create_bm_job(GLOBAL_STATE,
                                                        &template_ctx,
                                                        miner_address,
                                                        stratum_user && stratum_user[0] ? stratum_user : miner_address,
                                                        extranonce_counter++,
                                                        submit_difficulty,
                                                        &build_ms);
            GLOBAL_STATE->SYSTEM_MODULE.gridpool_template_build_ms = build_ms;
            if (!job) {
                GLOBAL_STATE->SYSTEM_MODULE.gridpool_template_failures++;
                set_template_status(&GLOBAL_STATE->SYSTEM_MODULE, false, 0, "Build failed", "Could not build GridPool ASIC work");
                vTaskDelay(pdMS_TO_TICKS(GRIDPOOL_DIRECT_RETRY_DELAY_MS));
                continue;
            }

            GLOBAL_STATE->SYSTEM_MODULE.gridpool_template_coinbase_bytes =
                ((gridpool_direct_work_context_t *)job->source_data)->job.coinbase_tx_len;
            GLOBAL_STATE->SYSTEM_MODULE.gridpool_template_slot0_value_sats =
                ((gridpool_direct_work_context_t *)job->source_data)->job.slot0_value_sats;
            GLOBAL_STATE->SYSTEM_MODULE.gridpool_direct_jobs_sent++;
            GLOBAL_STATE->SYSTEM_MODULE.work_received++;
            set_template_status(&GLOBAL_STATE->SYSTEM_MODULE,
                                true,
                                200,
                                template_ctx.split_rewards ? "Mining GridPool split work" : "Mining Solo Yolo work",
                                "");
            ASIC_send_work(GLOBAL_STATE, job);

            int delay_ms = ASIC_get_asic_job_frequency_ms(GLOBAL_STATE);
            if (delay_ms < 100) {
                delay_ms = 100;
            }
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }

        if (!nvs_config_get_bool(NVS_CONFIG_GRIDPOOL_ENABLED)) {
            gridpool_direct_template_context_free(&template_ctx);
        } else {
            set_template_status(&GLOBAL_STATE->SYSTEM_MODULE, true, 200, "Refreshing soon", "");
        }

        free(boot_url);
        free(rpc_url);
        free(rpc_auth_mode);
        free(rpc_username);
        free(rpc_password);
        free(payout_address);
        free(stratum_user);
    }
}

esp_err_t gridpool_status_client_start(GlobalState *GLOBAL_STATE)
{
    if (!payout_cache_mutex) {
        payout_cache_mutex = xSemaphoreCreateMutex();
        if (!payout_cache_mutex) {
            ESP_LOGE(TAG, "Error creating GridPool payout cache mutex");
            return ESP_FAIL;
        }
    }
    if (!gridpool_http_mutex) {
        gridpool_http_mutex = xSemaphoreCreateMutex();
        if (!gridpool_http_mutex) {
            ESP_LOGE(TAG, "Error creating GridPool HTTP mutex");
            return ESP_FAIL;
        }
    }

    if (xTaskCreate(gridpool_status_task, "gridpool_status", 8192, (void *)GLOBAL_STATE, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Error creating GridPool status task");
        return ESP_FAIL;
    }
    if (xTaskCreateWithCaps(gridpool_template_probe_task,
                            "gridpool_template",
                            12288,
                            (void *)GLOBAL_STATE,
                            4,
                            NULL,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGE(TAG, "Error creating GridPool template probe task");
        return ESP_FAIL;
    }
    return ESP_OK;
}
