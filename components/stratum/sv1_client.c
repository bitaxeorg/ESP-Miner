#include "sv1_client.h"

#include "sv1_protocol.h"

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_transport_ssl.h"
#include "esp_transport_tcp.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

static const char *TAG = "sv1_client";

#define TRANSPORT_TIMEOUT_MS 5000
#define BUFFER_SIZE 1024
#define JSON_RPC_BUFFER_LIMIT (STRATUM_V1_MAX_JSON_LINE_SIZE + 2U)

static char * json_rpc_buffer = NULL;
static size_t json_rpc_buffer_size = 0;
static size_t json_rpc_buffer_len = 0;

typedef struct
{
    int64_t timestamp_us;
    bool tracking;
} request_timing_t;

static request_timing_t *request_timings = NULL;

static request_timing_t *get_request_timing(int request_id) {
    if (request_id < 0) return NULL;
    int index = request_id % MAX_REQUEST_IDS;
    return &request_timings[index];
}

float STRATUM_V1_get_response_time_ms(int request_id, int64_t receive_time_us)
{
    if (request_id < 0) return -1.0;

    request_timing_t *timing = get_request_timing(request_id);
    if (!timing || !timing->tracking) {
        return -1.0;
    }

    float response_time = (receive_time_us - timing->timestamp_us) / 1000.0f;
    timing->tracking = false;
    return response_time;
}

esp_transport_handle_t STRATUM_V1_transport_init(tls_mode tls, const char *cert)
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

    if (request_timings == NULL) {
        request_timings = heap_caps_malloc(sizeof(request_timing_t) * MAX_REQUEST_IDS, MALLOC_CAP_SPIRAM);
        if (request_timings == NULL) {
            request_timings = malloc(sizeof(request_timing_t) * MAX_REQUEST_IDS);
        }
        if (request_timings == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for request_timings");
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

static void stamp_tx(int request_id, uint64_t timestamp_us)
{
    if (request_id >= 1) {
        request_timing_t *timing = get_request_timing(request_id);
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
    char subscribe_msg[BUFFER_SIZE];
    const esp_app_desc_t *app_desc = esp_app_get_description();
    int length = STRATUM_V1_encode_subscribe(subscribe_msg, sizeof(subscribe_msg), send_uid,
                                             model, app_desc->version);
    if (length < 0) return -1;
    debug_stratum_tx(subscribe_msg);

    return esp_transport_write(transport, subscribe_msg, length, TRANSPORT_TIMEOUT_MS);
}

int STRATUM_V1_suggest_difficulty(esp_transport_handle_t transport, int send_uid, uint32_t difficulty)
{
    char difficulty_msg[BUFFER_SIZE];
    int length = STRATUM_V1_encode_suggest_difficulty(difficulty_msg, sizeof(difficulty_msg),
                                                      send_uid, difficulty);
    if (length < 0) return -1;
    debug_stratum_tx(difficulty_msg);

    return esp_transport_write(transport, difficulty_msg, length, TRANSPORT_TIMEOUT_MS);
}

int STRATUM_V1_extranonce_subscribe(esp_transport_handle_t transport, int send_uid)
{
    char extranonce_msg[BUFFER_SIZE];
    int length = STRATUM_V1_encode_extranonce_subscribe(extranonce_msg, sizeof(extranonce_msg), send_uid);
    if (length < 0) return -1;
    debug_stratum_tx(extranonce_msg);

    return esp_transport_write(transport, extranonce_msg, length, TRANSPORT_TIMEOUT_MS);
}

int STRATUM_V1_authorize(esp_transport_handle_t transport, int send_uid, const char * username, const char * pass)
{
    char authorize_msg[BUFFER_SIZE];
    int length = STRATUM_V1_encode_authorize(authorize_msg, sizeof(authorize_msg), send_uid,
                                             username, pass);
    if (length < 0) return -1;
    debug_stratum_tx(authorize_msg);

    return esp_transport_write(transport, authorize_msg, length, TRANSPORT_TIMEOUT_MS);
}

int STRATUM_V1_pong(esp_transport_handle_t transport, int message_id)
{
    char pong_msg[BUFFER_SIZE];
    int length = STRATUM_V1_encode_pong(pong_msg, sizeof(pong_msg), message_id);
    if (length < 0) return -1;
    debug_stratum_tx(pong_msg);

    return esp_transport_write(transport, pong_msg, length, TRANSPORT_TIMEOUT_MS);
}

int STRATUM_V1_send_version(esp_transport_handle_t transport, int message_id)
{
    char version_msg[BUFFER_SIZE];
    const esp_app_desc_t *app_desc = esp_app_get_description();
    int length = STRATUM_V1_encode_version_response(version_msg, sizeof(version_msg), message_id,
                                                    app_desc->version);
    if (length < 0) return -1;
    debug_stratum_tx(version_msg);

    return esp_transport_write(transport, version_msg, length, TRANSPORT_TIMEOUT_MS);
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
    int length = STRATUM_V1_encode_submit_share(submit_msg, sizeof(submit_msg), send_uid,
                                                username, job_id, extranonce_2, ntime,
                                                nonce, version_bits);
    if (length < 0) return -1;

    int ret = esp_transport_write(transport, submit_msg, length, TRANSPORT_TIMEOUT_MS);

    uint64_t now = esp_timer_get_time();
    if (out_sent_time_us) {
        *out_sent_time_us = now;
    }

    debug_stratum_tx(submit_msg);

    stamp_tx(send_uid, now);

    return ret;
}

int STRATUM_V1_configure_version_rolling(esp_transport_handle_t transport, int send_uid)
{
    char configure_msg[BUFFER_SIZE];
    int length = STRATUM_V1_encode_configure_version_rolling(configure_msg, sizeof(configure_msg), send_uid);
    if (length < 0) return -1;
    debug_stratum_tx(configure_msg);

    return esp_transport_write(transport, configure_msg, length, TRANSPORT_TIMEOUT_MS);
}
