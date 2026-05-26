#include "local_work_client.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#include "asic.h"
#include "local_work_builder.h"
#include "nvs_config.h"
#include "system.h"
#include "utils.h"

#define LOCAL_WORK_TEMPLATE_INITIAL_DELAY_MS 15000
#define LOCAL_WORK_TEMPLATE_ERROR_INTERVAL_MS 30000
#define LOCAL_WORK_TEMPLATE_UNCONFIGURED_INTERVAL_MS 30000
#define LOCAL_WORK_TEMPLATE_REFRESH_MS 60000
#define LOCAL_WORK_RETRY_DELAY_MS 5000
#define LOCAL_WORK_RPC_TIMEOUT_MS 30000
#define LOCAL_WORK_INITIAL_RESPONSE_BYTES 2048
#define LOCAL_WORK_MAX_TEMPLATE_RESPONSE_BYTES (5 * 1024 * 1024)
#define LOCAL_WORK_TEMPLATE_TAG "/Local Work ESP32/"

static const char *TAG = "local_work";

typedef struct {
    local_work_template_t template_data;
    uint8_t *merkle_path;
    size_t merkle_path_count;
    int64_t fetched_us;
    bool valid;
} local_work_template_context_t;

typedef struct {
    local_work_job_t job;
    local_work_tx_bundle_t *tx_bundle;
    char miner_address[LOCAL_WORK_ADDRESS_MAX_LEN];
} local_work_context_t;

typedef struct {
    char *data;
    int len;
    int capacity;
    int max_bytes;
    int content_length;
    bool overflow;
} http_response_t;

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
    if (esp_psram_is_initialized()) {
        return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    return realloc(ptr, size);
}

static void set_template_status(SystemModule *module, bool reachable, int http_status, const char *status, const char *error)
{
    module->local_work_template_reachable = reachable;
    module->local_work_template_http_status = http_status;
    snprintf(module->local_work_template_status, sizeof(module->local_work_template_status), "%s", status ? status : "");
    snprintf(module->local_work_template_last_error, sizeof(module->local_work_template_last_error), "%s", error ? error : "");
}

static void set_block_status(SystemModule *module, int http_status, const char *status, const char *error)
{
    module->local_work_block_http_status = http_status;
    snprintf(module->local_work_block_status, sizeof(module->local_work_block_status), "%s", status ? status : "");
    snprintf(module->local_work_block_last_error, sizeof(module->local_work_block_last_error), "%s", error ? error : "");
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_response_t *response = (http_response_t *)evt->user_data;
    if (!response) {
        return ESP_OK;
    }

    if (evt->event_id == HTTP_EVENT_ON_HEADER &&
        evt->header_key &&
        strcasecmp(evt->header_key, "Content-Length") == 0 &&
        evt->header_value) {
        response->content_length = atoi(evt->header_value);
    } else if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0) {
        if (response->len + evt->data_len + 1 > response->max_bytes) {
            response->overflow = true;
            return ESP_FAIL;
        }
        if (response->len + evt->data_len + 1 > response->capacity) {
            int next_capacity = response->capacity;
            while (next_capacity < response->len + evt->data_len + 1) {
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
    }
    return ESP_OK;
}

static esp_err_t fetch_rpc_json(const char *rpc_url,
                                const char *rpc_username,
                                const char *rpc_password,
                                const char *payload,
                                char **body_out,
                                int *body_len_out,
                                int *http_status_out,
                                int64_t *elapsed_ms_out)
{
    if (!rpc_url || !rpc_url[0] || !payload || !body_out || !body_len_out || !http_status_out) {
        return ESP_ERR_INVALID_ARG;
    }

    *body_out = NULL;
    *body_len_out = 0;
    *http_status_out = 0;

    http_response_t response = {
        .data = response_malloc(LOCAL_WORK_INITIAL_RESPONSE_BYTES),
        .capacity = LOCAL_WORK_INITIAL_RESPONSE_BYTES,
        .max_bytes = LOCAL_WORK_MAX_TEMPLATE_RESPONSE_BYTES,
    };
    if (!response.data) {
        return ESP_ERR_NO_MEM;
    }
    response.data[0] = '\0';

    esp_http_client_config_t config = {
        .url = rpc_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = LOCAL_WORK_RPC_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .user_data = &response,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(response.data);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (rpc_username && rpc_username[0]) {
        esp_http_client_set_username(client, rpc_username);
        esp_http_client_set_password(client, rpc_password ? rpc_password : "");
        esp_http_client_set_authtype(client, HTTP_AUTH_TYPE_BASIC);
    }
    esp_http_client_set_post_field(client, payload, strlen(payload));

    int64_t start_us = esp_timer_get_time();
    esp_err_t err = esp_http_client_perform(client);
    if (elapsed_ms_out) {
        *elapsed_ms_out = (esp_timer_get_time() - start_us) / 1000;
    }
    *http_status_out = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || response.overflow) {
        free(response.data);
        return response.overflow ? ESP_ERR_NO_MEM : err;
    }

    *body_out = response.data;
    *body_len_out = response.len;
    return ESP_OK;
}

static void local_work_template_context_free(local_work_template_context_t *ctx)
{
    if (!ctx) {
        return;
    }
    local_work_template_free(&ctx->template_data);
    free(ctx->merkle_path);
    memset(ctx, 0, sizeof(*ctx));
}

static void local_work_context_free(void *ptr)
{
    local_work_context_t *ctx = (local_work_context_t *)ptr;
    if (!ctx) {
        return;
    }
    local_work_job_free(&ctx->job);
    local_work_tx_bundle_release(ctx->tx_bundle);
    free(ctx);
}

static bool choose_payout_address(const char *configured, const char *stratum_user, char *dest, size_t dest_len)
{
    if (!dest || dest_len == 0) {
        return false;
    }
    dest[0] = '\0';
    const char *source = configured && configured[0] ? configured : stratum_user;
    if (!source || !source[0]) {
        return false;
    }
    size_t len = 0;
    while (source[len] && source[len] != '.' && len + 1 < dest_len) {
        dest[len] = source[len];
        len++;
    }
    dest[len] = '\0';
    return len > 0;
}

static void record_heap_after(SystemModule *module)
{
    module->local_work_template_heap_after = esp_get_free_heap_size();
    module->local_work_template_internal_heap_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    module->local_work_template_spiram_heap_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

static esp_err_t refresh_template(GlobalState *GLOBAL_STATE,
                                  const char *rpc_url,
                                  const char *rpc_auth_mode,
                                  const char *rpc_username,
                                  const char *rpc_password,
                                  const char *payout_address,
                                  local_work_template_context_t *ctx)
{
    static const char *GBT_PAYLOAD =
        "{\"jsonrpc\":\"1.0\",\"id\":\"local-work-template\","
        "\"method\":\"getblocktemplate\",\"params\":[{\"rules\":[\"segwit\"]}]}";

    SystemModule *module = &GLOBAL_STATE->SYSTEM_MODULE;
    local_work_template_context_t next = { 0 };
    char *template_body = NULL;
    int template_body_len = 0;
    int rpc_http_status = 0;
    int64_t rpc_ms = 0;
    char parse_stage[64] = "";
    uint8_t zero_coinbase_txid[32] = { 0 };
    uint8_t ignored_merkle_root[32] = { 0 };
    int64_t total_start_us = esp_timer_get_time();

    module->local_work_template_last_attempt_time = now_seconds();
    module->local_work_template_heap_before = esp_get_free_heap_size();
    module->local_work_template_internal_heap_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    module->local_work_template_spiram_heap_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    set_template_status(module, false, 0, "Refreshing template", "");

    bool use_basic_auth = !rpc_auth_mode || rpc_auth_mode[0] == '\0' || strcmp(rpc_auth_mode, "basic") == 0;
    esp_err_t err = fetch_rpc_json(rpc_url,
                                   use_basic_auth ? rpc_username : NULL,
                                   use_basic_auth ? rpc_password : NULL,
                                   GBT_PAYLOAD,
                                   &template_body,
                                   &template_body_len,
                                   &rpc_http_status,
                                   &rpc_ms);
    module->local_work_template_rpc_ms = (uint32_t)rpc_ms;
    module->local_work_template_http_status = rpc_http_status;
    module->local_work_template_bytes = template_body_len > 0 ? (uint32_t)template_body_len : 0;
    if (err != ESP_OK) {
        char error[LOCAL_WORK_TEMPLATE_ERROR_LEN];
        snprintf(error, sizeof(error), "getblocktemplate failed (%s, HTTP %d, %d bytes)",
                 esp_err_to_name(err), rpc_http_status, template_body_len);
        set_template_status(module, false, rpc_http_status, "RPC fetch failed", error);
        goto fail;
    }

    int64_t parse_start_us = esp_timer_get_time();
    err = local_work_parse_template_json_buffer(template_body, &next.template_data, parse_stage, sizeof(parse_stage));
    module->local_work_template_parse_ms = (uint32_t)((esp_timer_get_time() - parse_start_us) / 1000);
    template_body = NULL;
    if (err != ESP_OK) {
        char error[LOCAL_WORK_TEMPLATE_ERROR_LEN];
        snprintf(error, sizeof(error), "Template parse failed at %s (%s, %d bytes)",
                 parse_stage[0] ? parse_stage : "unknown", esp_err_to_name(err), template_body_len);
        set_template_status(module, false, rpc_http_status, "Parse failed", error);
        goto fail;
    }

    uint32_t coinbase_weight = 0;
    size_t coinbase_full_len = 0;
    err = local_work_estimate_coinbase_weight(&next.template_data,
                                              payout_address,
                                              NULL,
                                              0,
                                              8,
                                              LOCAL_WORK_TEMPLATE_TAG,
                                              &coinbase_weight,
                                              &coinbase_full_len);
    if (err != ESP_OK) {
        set_template_status(module, false, rpc_http_status, "Build failed", "Could not estimate coinbase weight");
        goto fail;
    }

    err = local_work_trim_template_to_weight(&next.template_data, coinbase_weight);
    if (err != ESP_OK) {
        set_template_status(module, false, rpc_http_status, "Build failed", "Could not trim template to weight limit");
        goto fail;
    }

    int64_t build_start_us = esp_timer_get_time();
    err = local_work_compute_merkle_path(zero_coinbase_txid,
                                         next.template_data.txid_hashes,
                                         next.template_data.tx_count,
                                         &next.merkle_path,
                                         &next.merkle_path_count,
                                         ignored_merkle_root);
    module->local_work_template_build_ms = (uint32_t)((esp_timer_get_time() - build_start_us) / 1000);
    if (err != ESP_OK) {
        set_template_status(module, false, rpc_http_status, "Build failed", "Could not compute merkle path");
        goto fail;
    }

    next.fetched_us = esp_timer_get_time();
    next.valid = true;

    local_work_template_context_free(ctx);
    *ctx = next;
    memset(&next, 0, sizeof(next));

    module->local_work_template_runs++;
    module->local_work_template_last_success_time = now_seconds();
    module->local_work_template_height = ctx->template_data.height;
    module->local_work_template_tx_count = ctx->template_data.tx_count;
    module->local_work_template_merkle_path_count = ctx->merkle_path_count;
    module->local_work_template_coinbase_bytes = (uint32_t)coinbase_full_len;
    module->local_work_template_total_ms = (uint32_t)((esp_timer_get_time() - total_start_us) / 1000);
    module->local_work_template_slot0_value_sats = ctx->template_data.coinbase_value;
    snprintf(module->local_work_template_prev_hash,
             sizeof(module->local_work_template_prev_hash),
             "%s",
             ctx->template_data.previous_block_hash);
    GLOBAL_STATE->block_height = ctx->template_data.height;
    GLOBAL_STATE->network_nonce_diff = (uint64_t)networkDifficulty(ctx->template_data.nbits);
    suffixString(GLOBAL_STATE->network_nonce_diff, GLOBAL_STATE->network_diff_string, DIFF_STRING_SIZE, 0);
    SYSTEM_notify_new_ntime(GLOBAL_STATE, ctx->template_data.curtime);
    set_template_status(module, true, rpc_http_status, "Template cached", "");
    record_heap_after(module);
    return ESP_OK;

fail:
    module->local_work_template_failures++;
    free(template_body);
    local_work_template_context_free(&next);
    record_heap_after(module);
    return err;
}

static bm_job *create_local_work_job(GlobalState *GLOBAL_STATE,
                                     const local_work_template_context_t *template_ctx,
                                     const char *payout_address,
                                     uint64_t extranonce_counter)
{
    if (!template_ctx || !template_ctx->valid || !payout_address || !payout_address[0]) {
        return NULL;
    }

    local_work_context_t *work_ctx = response_malloc(sizeof(*work_ctx));
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

    uint32_t build_ms = 0;
    int64_t build_start_us = esp_timer_get_time();
    esp_err_t err = local_work_build_job_from_path(&template_ctx->template_data,
                                                   payout_address,
                                                   NULL,
                                                   0,
                                                   template_ctx->merkle_path,
                                                   template_ctx->merkle_path_count,
                                                   extranonce,
                                                   sizeof(extranonce),
                                                   LOCAL_WORK_TEMPLATE_TAG,
                                                   networkDifficulty(template_ctx->template_data.nbits),
                                                   &work_ctx->job);
    build_ms = (uint32_t)((esp_timer_get_time() - build_start_us) / 1000);
    GLOBAL_STATE->SYSTEM_MODULE.local_work_template_build_ms = build_ms;
    if (err != ESP_OK) {
        local_work_context_free(work_ctx);
        return NULL;
    }

    snprintf(work_ctx->miner_address, sizeof(work_ctx->miner_address), "%s", payout_address);
    work_ctx->tx_bundle = template_ctx->template_data.tx_bundle;
    local_work_tx_bundle_retain(work_ctx->tx_bundle);

    GLOBAL_STATE->block_height = template_ctx->template_data.height;
    snprintf(GLOBAL_STATE->scriptsig, sizeof(GLOBAL_STATE->scriptsig), "%s", LOCAL_WORK_TEMPLATE_TAG);
    memset(GLOBAL_STATE->coinbase_outputs, 0, sizeof(GLOBAL_STATE->coinbase_outputs));
    GLOBAL_STATE->coinbase_outputs[0].value_satoshis = work_ctx->job.slot0_value_sats;
    snprintf(GLOBAL_STATE->coinbase_outputs[0].address,
             sizeof(GLOBAL_STATE->coinbase_outputs[0].address),
             "%s",
             payout_address);
    GLOBAL_STATE->coinbase_outputs[0].is_user_output = true;
    GLOBAL_STATE->coinbase_output_count = 1;
    GLOBAL_STATE->coinbase_value_total_satoshis = template_ctx->template_data.coinbase_value;
    GLOBAL_STATE->coinbase_value_user_satoshis = work_ctx->job.slot0_value_sats;

    bm_job *job = calloc(1, sizeof(*job));
    if (!job) {
        local_work_context_free(work_ctx);
        return NULL;
    }

    *job = work_ctx->job.bm_job;
    char job_id[16];
    snprintf(job_id, sizeof(job_id), "lw%08" PRIx32, GLOBAL_STATE->SYSTEM_MODULE.local_work_direct_jobs_sent + 1);
    job->jobid = strdup(job_id);
    job->source = BM_JOB_SOURCE_LOCAL_WORK;
    job->source_data = work_ctx;
    job->source_data_free = local_work_context_free;
    return job;
}

bool local_work_is_enabled(void)
{
    return nvs_config_get_bool(NVS_CONFIG_LOCAL_WORK_ENABLED);
}

static esp_err_t serialize_bm_job_header(const bm_job *job,
                                         uint32_t nonce,
                                         uint32_t rolled_version,
                                         uint8_t header[80])
{
    if (!job || job->source != BM_JOB_SOURCE_LOCAL_WORK || !job->source_data || !header) {
        return ESP_ERR_INVALID_ARG;
    }
    local_work_context_t *ctx = (local_work_context_t *)job->source_data;
    return local_work_serialize_header(&ctx->job, nonce, rolled_version, header);
}

static size_t encode_varint(uint64_t value, uint8_t out[9])
{
    if (value < 0xfd) {
        out[0] = (uint8_t)value;
        return 1;
    }
    if (value <= 0xffff) {
        out[0] = 0xfd;
        out[1] = value & 0xff;
        out[2] = (value >> 8) & 0xff;
        return 3;
    }
    if (value <= 0xffffffffULL) {
        out[0] = 0xfe;
        for (int i = 0; i < 4; i++) {
            out[1 + i] = (value >> (8 * i)) & 0xff;
        }
        return 5;
    }
    out[0] = 0xff;
    for (int i = 0; i < 8; i++) {
        out[1 + i] = (value >> (8 * i)) & 0xff;
    }
    return 9;
}

static esp_err_t http_write_all(esp_http_client_handle_t client, const char *data, int len)
{
    int written = 0;
    while (written < len) {
        int ret = esp_http_client_write(client, data + written, len - written);
        if (ret < 0) {
            return ESP_FAIL;
        }
        written += ret;
    }
    return ESP_OK;
}

static esp_err_t http_write_hex_bytes(esp_http_client_handle_t client, const uint8_t *data, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    char buffer[256];
    size_t offset = 0;
    while (offset < len) {
        size_t chunk = len - offset;
        if (chunk > sizeof(buffer) / 2) {
            chunk = sizeof(buffer) / 2;
        }
        for (size_t i = 0; i < chunk; i++) {
            buffer[i * 2] = hex[data[offset + i] >> 4];
            buffer[i * 2 + 1] = hex[data[offset + i] & 0x0f];
        }
        ESP_RETURN_ON_ERROR(http_write_all(client, buffer, chunk * 2), TAG, "write hex chunk");
        offset += chunk;
    }
    return ESP_OK;
}

esp_err_t local_work_submit_block(GlobalState *GLOBAL_STATE,
                                  const bm_job *job,
                                  uint32_t nonce,
                                  uint32_t rolled_version)
{
    if (!GLOBAL_STATE || !job || job->source != BM_JOB_SOURCE_LOCAL_WORK || !job->source_data) {
        return ESP_ERR_INVALID_ARG;
    }

    local_work_context_t *ctx = (local_work_context_t *)job->source_data;
    if (!ctx->tx_bundle || !ctx->job.coinbase_tx || ctx->job.coinbase_tx_len == 0) {
        set_block_status(&GLOBAL_STATE->SYSTEM_MODULE, 0, "Unavailable", "No retained template transactions for submitblock");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t header[80];
    ESP_RETURN_ON_ERROR(serialize_bm_job_header(job, nonce, rolled_version, header), TAG, "serialize header");

    uint8_t tx_count_varint[9];
    size_t tx_count_varint_len = encode_varint(ctx->tx_bundle->tx_count + 1, tx_count_varint);
    size_t block_bytes = sizeof(header) + tx_count_varint_len + ctx->job.coinbase_tx_len;
    for (size_t i = 0; i < ctx->tx_bundle->tx_count; i++) {
        block_bytes += ctx->tx_bundle->tx_lengths[i];
    }

    char *rpc_url = nvs_config_get_string(NVS_CONFIG_LOCAL_WORK_BITCOIN_RPC_URL);
    char *rpc_auth_mode = nvs_config_get_string(NVS_CONFIG_LOCAL_WORK_BITCOIN_RPC_AUTH_MODE);
    char *rpc_username = nvs_config_get_string(NVS_CONFIG_LOCAL_WORK_BITCOIN_RPC_USERNAME);
    char *rpc_password = nvs_config_get_string(NVS_CONFIG_LOCAL_WORK_BITCOIN_RPC_PASSWORD);
    bool use_basic_auth = !rpc_auth_mode || rpc_auth_mode[0] == '\0' || strcmp(rpc_auth_mode, "basic") == 0;
    if (!rpc_url || !rpc_url[0]) {
        free(rpc_url);
        free(rpc_auth_mode);
        free(rpc_username);
        free(rpc_password);
        set_block_status(&GLOBAL_STATE->SYSTEM_MODULE, 0, "RPC not configured", "Bitcoin RPC submitblock is not configured");
        return ESP_ERR_INVALID_STATE;
    }

    http_response_t response = {
        .data = response_malloc(LOCAL_WORK_INITIAL_RESPONSE_BYTES),
        .capacity = LOCAL_WORK_INITIAL_RESPONSE_BYTES,
        .max_bytes = LOCAL_WORK_INITIAL_RESPONSE_BYTES,
    };
    if (!response.data) {
        free(rpc_url);
        free(rpc_auth_mode);
        free(rpc_username);
        free(rpc_password);
        return ESP_ERR_NO_MEM;
    }
    response.data[0] = '\0';

    esp_http_client_config_t config = {
        .url = rpc_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = LOCAL_WORK_RPC_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .user_data = &response,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(response.data);
        free(rpc_url);
        free(rpc_auth_mode);
        free(rpc_username);
        free(rpc_password);
        return ESP_FAIL;
    }

    if (use_basic_auth && rpc_username && rpc_username[0]) {
        esp_http_client_set_username(client, rpc_username);
        esp_http_client_set_password(client, rpc_password ? rpc_password : "");
        esp_http_client_set_authtype(client, HTTP_AUTH_TYPE_BASIC);
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");

    const char *prefix = "{\"jsonrpc\":\"1.0\",\"id\":\"local-work-submitblock\",\"method\":\"submitblock\",\"params\":[\"";
    const char *suffix = "\"]}";
    int content_len = strlen(prefix) + (int)(block_bytes * 2) + strlen(suffix);
    esp_err_t err = esp_http_client_open(client, content_len);
    if (err != ESP_OK) {
        set_block_status(&GLOBAL_STATE->SYSTEM_MODULE, 0, "Submit failed", "Could not open Bitcoin RPC submitblock request");
        esp_http_client_cleanup(client);
        free(response.data);
        free(rpc_url);
        free(rpc_auth_mode);
        free(rpc_username);
        free(rpc_password);
        return err;
    }

    SystemModule *module = &GLOBAL_STATE->SYSTEM_MODULE;
    module->local_work_block_submits++;
    module->local_work_block_last_submit_time = now_seconds();
    int64_t start_us = esp_timer_get_time();

    err = http_write_all(client, prefix, strlen(prefix));
    if (err == ESP_OK) err = http_write_hex_bytes(client, header, sizeof(header));
    if (err == ESP_OK) err = http_write_hex_bytes(client, tx_count_varint, tx_count_varint_len);
    if (err == ESP_OK) err = http_write_hex_bytes(client, ctx->job.coinbase_tx, ctx->job.coinbase_tx_len);
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
    if (err == ESP_OK) err = http_write_all(client, suffix, strlen(suffix));
    if (err == ESP_OK) err = esp_http_client_fetch_headers(client) >= 0 ? ESP_OK : ESP_FAIL;

    int http_status = esp_http_client_get_status_code(client);
    module->local_work_block_submit_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000);
    module->local_work_block_http_status = http_status;
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(rpc_url);
    free(rpc_auth_mode);
    free(rpc_username);
    free(rpc_password);

    if (err != ESP_OK || http_status < 200 || http_status >= 300) {
        char error[LOCAL_WORK_BLOCK_ERROR_LEN];
        snprintf(error, sizeof(error), "submitblock failed (%s, HTTP %d)", esp_err_to_name(err), http_status);
        module->local_work_block_rejected++;
        set_block_status(module, http_status, "Submit failed", error);
        free(response.data);
        return err != ESP_OK ? err : ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(response.data);
    cJSON *result = root ? cJSON_GetObjectItem(root, "result") : NULL;
    cJSON *error = root ? cJSON_GetObjectItem(root, "error") : NULL;
    if (root && cJSON_IsNull(result) && (!error || cJSON_IsNull(error))) {
        module->local_work_block_accepted++;
        set_block_status(module, http_status, "Accepted", "");
        cJSON_Delete(root);
        free(response.data);
        return ESP_OK;
    }

    char error_text[LOCAL_WORK_BLOCK_ERROR_LEN] = "submitblock rejected";
    if (cJSON_IsString(result) && result->valuestring) {
        snprintf(error_text, sizeof(error_text), "%s", result->valuestring);
    } else if (cJSON_IsObject(error)) {
        cJSON *message = cJSON_GetObjectItem(error, "message");
        if (cJSON_IsString(message) && message->valuestring) {
            snprintf(error_text, sizeof(error_text), "%s", message->valuestring);
        }
    }
    module->local_work_block_rejected++;
    set_block_status(module, http_status, "Rejected", error_text);
    cJSON_Delete(root);
    free(response.data);
    return ESP_FAIL;
}

static void local_work_task(void *pvParameters)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;
    local_work_template_context_t template_ctx = { 0 };
    uint64_t extranonce_counter = ((uint64_t)esp_random() << 32) ^ (uint64_t)esp_timer_get_time();

    vTaskDelay(pdMS_TO_TICKS(LOCAL_WORK_TEMPLATE_INITIAL_DELAY_MS));

    while (1) {
        char *rpc_url = nvs_config_get_string(NVS_CONFIG_LOCAL_WORK_BITCOIN_RPC_URL);
        char *rpc_auth_mode = nvs_config_get_string(NVS_CONFIG_LOCAL_WORK_BITCOIN_RPC_AUTH_MODE);
        char *rpc_username = nvs_config_get_string(NVS_CONFIG_LOCAL_WORK_BITCOIN_RPC_USERNAME);
        char *rpc_password = nvs_config_get_string(NVS_CONFIG_LOCAL_WORK_BITCOIN_RPC_PASSWORD);
        char *payout_address_config = nvs_config_get_string(NVS_CONFIG_LOCAL_WORK_PAYOUT_ADDRESS);
        char *stratum_user = nvs_config_get_string(NVS_CONFIG_STRATUM_USER);

        if (!local_work_is_enabled()) {
            local_work_template_context_free(&template_ctx);
            set_template_status(&GLOBAL_STATE->SYSTEM_MODULE, false, 0, "Disabled", "");
            goto unconfigured_delay;
        }
        if (!rpc_url || !rpc_url[0]) {
            local_work_template_context_free(&template_ctx);
            set_template_status(&GLOBAL_STATE->SYSTEM_MODULE, false, 0, "RPC not configured", "Bitcoin RPC URL is empty");
            goto unconfigured_delay;
        }

        bool use_basic_auth = !rpc_auth_mode || rpc_auth_mode[0] == '\0' || strcmp(rpc_auth_mode, "basic") == 0;
        if (use_basic_auth && (!rpc_username || !rpc_username[0])) {
            local_work_template_context_free(&template_ctx);
            set_template_status(&GLOBAL_STATE->SYSTEM_MODULE, false, 0, "RPC not configured", "Bitcoin RPC username is empty");
            goto unconfigured_delay;
        }

        char payout_address[LOCAL_WORK_ADDRESS_MAX_LEN];
        if (!choose_payout_address(payout_address_config, stratum_user, payout_address, sizeof(payout_address))) {
            local_work_template_context_free(&template_ctx);
            set_template_status(&GLOBAL_STATE->SYSTEM_MODULE, false, 0, "Payout not configured", "Local payout address is empty");
            goto unconfigured_delay;
        }

        esp_err_t err = refresh_template(GLOBAL_STATE, rpc_url, rpc_auth_mode, rpc_username, rpc_password, payout_address, &template_ctx);
        if (err != ESP_OK && !template_ctx.valid) {
            goto error_delay;
        }

        int64_t refresh_started_us = esp_timer_get_time();
        while (local_work_is_enabled() &&
               template_ctx.valid &&
               ((esp_timer_get_time() - refresh_started_us) / 1000) < LOCAL_WORK_TEMPLATE_REFRESH_MS) {
            if (!GLOBAL_STATE->ASIC_initalized || !GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs) {
                set_template_status(&GLOBAL_STATE->SYSTEM_MODULE, true, 0, "Waiting for ASIC", "");
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }

            bm_job *job = create_local_work_job(GLOBAL_STATE, &template_ctx, payout_address, extranonce_counter++);
            if (!job) {
                GLOBAL_STATE->SYSTEM_MODULE.local_work_template_failures++;
                set_template_status(&GLOBAL_STATE->SYSTEM_MODULE, false, 0, "Build failed", "Could not build local ASIC work");
                vTaskDelay(pdMS_TO_TICKS(LOCAL_WORK_RETRY_DELAY_MS));
                continue;
            }

            GLOBAL_STATE->SYSTEM_MODULE.local_work_template_coinbase_bytes =
                ((local_work_context_t *)job->source_data)->job.coinbase_tx_len;
            GLOBAL_STATE->SYSTEM_MODULE.local_work_template_slot0_value_sats =
                ((local_work_context_t *)job->source_data)->job.slot0_value_sats;
            GLOBAL_STATE->SYSTEM_MODULE.local_work_direct_jobs_sent++;
            GLOBAL_STATE->SYSTEM_MODULE.work_received++;
            set_template_status(&GLOBAL_STATE->SYSTEM_MODULE, true, 200, "Mining local work", "");
            ASIC_send_work(GLOBAL_STATE, job);

            int delay_ms = ASIC_get_asic_job_frequency_ms(GLOBAL_STATE);
            vTaskDelay(pdMS_TO_TICKS(delay_ms < 100 ? 100 : delay_ms));
        }

        if (!local_work_is_enabled()) {
            local_work_template_context_free(&template_ctx);
        } else {
            set_template_status(&GLOBAL_STATE->SYSTEM_MODULE, true, 200, "Refreshing soon", "");
        }

        free(rpc_url);
        free(rpc_auth_mode);
        free(rpc_username);
        free(rpc_password);
        free(payout_address_config);
        free(stratum_user);
        continue;

unconfigured_delay:
        free(rpc_url);
        free(rpc_auth_mode);
        free(rpc_username);
        free(rpc_password);
        free(payout_address_config);
        free(stratum_user);
        vTaskDelay(pdMS_TO_TICKS(LOCAL_WORK_TEMPLATE_UNCONFIGURED_INTERVAL_MS));
        continue;

error_delay:
        free(rpc_url);
        free(rpc_auth_mode);
        free(rpc_username);
        free(rpc_password);
        free(payout_address_config);
        free(stratum_user);
        vTaskDelay(pdMS_TO_TICKS(LOCAL_WORK_TEMPLATE_ERROR_INTERVAL_MS));
    }
}

esp_err_t local_work_client_start(GlobalState *GLOBAL_STATE)
{
    if (xTaskCreateWithCaps(local_work_task,
                            "local_work",
                            8192,
                            (void *)GLOBAL_STATE,
                            4,
                            NULL,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGE(TAG, "Error creating local work task");
        return ESP_FAIL;
    }
    return ESP_OK;
}
