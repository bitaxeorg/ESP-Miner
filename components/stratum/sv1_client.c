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

static const char *TAG = "sv1_client";

#define TRANSPORT_TIMEOUT_MS 5000
#define BUFFER_SIZE 1024
#define MAX_JSON_RPC_BUFFER_SIZE (32 * 1024)

static char * json_rpc_buffer = NULL;
static size_t json_rpc_buffer_size = 0;

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
        request_timings = heap_caps_malloc(sizeof(request_timing_t) * MAX_REQUEST_IDS, MALLOC_CAP_SPIRAM);
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
