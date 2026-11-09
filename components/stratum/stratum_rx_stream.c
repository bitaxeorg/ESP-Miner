#include "stratum_rx_stream.h"
#include "stratum_api.h"
#include "stratum_socket.h"
#include "utils.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#define BUFFER_SIZE 1024
#define JSON_RPC_BUFFER_LIMIT (STRATUM_V1_MAX_JSON_LINE_SIZE + 2U)
#define STRATUM_V1_COINB2_STREAM_THRESHOLD 4096U

static const char *TAG = "stratum_rx_stream";

static char *json_rpc_buffer = NULL;
static size_t json_rpc_buffer_size = 0;
static size_t json_rpc_buffer_len = 0;

static uint8_t *s_rx_suffix = NULL;
static size_t s_rx_suffix_capacity = 0;
static size_t s_rx_suffix_len = 0;
static size_t s_rx_coinb2_hex_count = 0;
static bool s_rx_streaming_coinb2 = false;
static bool s_rx_coinb2_checked = false;
static bool s_rx_suffix_valid = false;
static bool s_rx_suffix_error = false;
static char s_rx_hex_carry = '\0';

static void discard_coinb2(void)
{
    free(s_rx_suffix);
    s_rx_suffix = NULL;
    s_rx_suffix_capacity = 0;
    s_rx_suffix_len = 0;
    s_rx_suffix_valid = false;
}

static void reset_rx_line_state(void)
{
    s_rx_streaming_coinb2 = false;
    s_rx_coinb2_checked = false;
    s_rx_suffix_error = false;
    s_rx_hex_carry = '\0';
    s_rx_coinb2_hex_count = 0;
    discard_coinb2();
}

static void reset_json_rpc_buffer(void)
{
    reset_rx_line_state();
    json_rpc_buffer_len = 0;
    if (json_rpc_buffer != NULL) {
        json_rpc_buffer[0] = '\0';
    }
}

bool stratum_rx_stream_init(void)
{
    reset_rx_line_state();
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
        new_size = MIN(new_size * 2U, JSON_RPC_BUFFER_LIMIT);
    }

    uint32_t caps = MALLOC_CAP_8BIT;
    if (esp_psram_is_initialized()) caps |= MALLOC_CAP_SPIRAM;
    char *new_buffer = heap_caps_realloc(json_rpc_buffer, new_size, caps);
    if (new_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to grow JSON-RPC receive buffer to %zu bytes", new_size);
        return false;
    }

    json_rpc_buffer = new_buffer;
    json_rpc_buffer_size = new_size;
    return true;
}

static bool stream_coinb2_hex_bytes(const char *data, size_t len)
{
    if (len == 0) {
        return true;
    }
    if (len > (size_t)MAX_COINBASE_SUFFIX_LEN * 2U - s_rx_coinb2_hex_count) {
        ESP_LOGE(TAG, "Streamed coinbase_2 exceeds maximum suffix size (%u bytes)",
                 MAX_COINBASE_SUFFIX_LEN);
        return false;
    }
    s_rx_coinb2_hex_count += len;

    if (s_rx_suffix_error) {
        return true;
    }

    size_t total_hex = (s_rx_hex_carry != '\0' ? 1U : 0U) + len;
    size_t new_bytes = total_hex / 2U;
    if (s_rx_suffix_len + new_bytes > s_rx_suffix_capacity) {
        size_t new_cap = s_rx_suffix_capacity ? s_rx_suffix_capacity : 4096U;
        while (new_cap < s_rx_suffix_len + new_bytes && new_cap < MAX_COINBASE_SUFFIX_LEN) {
            new_cap = MIN(new_cap * 2U, (size_t)MAX_COINBASE_SUFFIX_LEN);
        }
        uint32_t caps = MALLOC_CAP_8BIT;
        if (esp_psram_is_initialized()) caps |= MALLOC_CAP_SPIRAM;
        uint8_t *new_buf = heap_caps_realloc(s_rx_suffix, new_cap, caps);
        if (!new_buf) {
            ESP_LOGE(TAG, "Failed to grow streamed coinbase suffix buffer to %zu bytes", new_cap);
            return false;
        }
        s_rx_suffix = new_buf;
        s_rx_suffix_capacity = new_cap;
    }

    size_t idx = 0;
    if (s_rx_hex_carry != '\0') {
        char pair[3] = {s_rx_hex_carry, data[0], '\0'};
        s_rx_hex_carry = '\0';
        if (hex2bin(pair, s_rx_suffix + s_rx_suffix_len, 1U) != 1U) {
            ESP_LOGE(TAG, "Invalid hex character in streamed coinbase_2");
            s_rx_suffix_error = true;
            return true;
        }
        s_rx_suffix_len++;
        idx = 1U;
    }

    size_t bulk_bytes = (len - idx) / 2U;
    if (bulk_bytes > 0) {
        if (hex2bin(data + idx, s_rx_suffix + s_rx_suffix_len, bulk_bytes) != bulk_bytes) {
            ESP_LOGE(TAG, "Invalid hex character in streamed coinbase_2");
            s_rx_suffix_error = true;
            return true;
        }
        s_rx_suffix_len += bulk_bytes;
        idx += bulk_bytes * 2U;
    }

    if (idx < len) {
        if (hex_val_table[(unsigned char)data[idx]] < 0) {
            ESP_LOGE(TAG, "Invalid hex character in streamed coinbase_2");
            s_rx_suffix_error = true;
            return true;
        }
        s_rx_hex_carry = data[idx];
    }

    return true;
}

static size_t skip_ws(const char *buf, size_t len, size_t i)
{
    while (i < len && (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\r' || buf[i] == '\n')) {
        i++;
    }
    return i;
}

static bool skip_json_string(const char *buf, size_t len, size_t *io_pos)
{
    size_t i = *io_pos;
    if (i >= len || buf[i] != '"') {
        return false;
    }
    i++;
    while (i < len) {
        if (buf[i] == '\\') {
            i += 2U;
            if (i > len) {
                return false;
            }
        } else if (buf[i] == '"') {
            *io_pos = i + 1U;
            return true;
        } else {
            i++;
        }
    }
    return false;
}

static bool skip_json_value(const char *buf, size_t len, size_t *io_pos)
{
    size_t i = skip_ws(buf, len, *io_pos);
    if (i >= len) {
        return false;
    }
    if (buf[i] == '"') {
        *io_pos = i;
        return skip_json_string(buf, len, io_pos);
    }
    if (buf[i] == '{' || buf[i] == '[') {
        int depth = 0;
        while (i < len) {
            if (buf[i] == '"') {
                *io_pos = i;
                if (!skip_json_string(buf, len, io_pos)) {
                    return false;
                }
                i = *io_pos;
                continue;
            }
            if (buf[i] == '{' || buf[i] == '[') {
                depth++;
            } else if (buf[i] == '}' || buf[i] == ']') {
                depth--;
                if (depth == 0) {
                    *io_pos = i + 1U;
                    return true;
                }
            }
            i++;
        }
        return false;
    }
    while (i < len && buf[i] != ',' && buf[i] != '}' && buf[i] != ']' &&
           buf[i] != ' ' && buf[i] != '\t' && buf[i] != '\r' && buf[i] != '\n') {
        i++;
    }
    *io_pos = i;
    return true;
}

static bool find_params_coinb2_quote(const char *buf, size_t len, size_t *out_coinb2_start)
{
    size_t i = skip_ws(buf, len, 0);
    if (i >= len || buf[i] != '{') {
        return false;
    }
    i++;

    while (i < len) {
        i = skip_ws(buf, len, i);
        if (i >= len || buf[i] != '"') {
            return false;
        }
        size_t key_start = i + 1U;
        if (!skip_json_string(buf, len, &i)) {
            return false;
        }
        size_t key_len = (i - 1U) - key_start;
        i = skip_ws(buf, len, i);
        if (i >= len || buf[i] != ':') {
            return false;
        }
        i = skip_ws(buf, len, i + 1U);

        if (key_len == 6U && memcmp(buf + key_start, "method", 6U) == 0) {
            if (i >= len || buf[i] != '"') {
                return false;
            }
            size_t val_start = i + 1U;
            if (!skip_json_string(buf, len, &i)) {
                return false;
            }
            size_t val_len = (i - 1U) - val_start;
            if (val_len != 13U || memcmp(buf + val_start, "mining.notify", 13U) != 0) {
                return false;
            }
            i = skip_ws(buf, len, i);
            if (i >= len || buf[i] != ',') {
                return false;
            }
            i++;
            continue;
        }

        if (key_len == 6U && memcmp(buf + key_start, "params", 6U) == 0) {
            if (i >= len || buf[i] != '[') {
                return false;
            }
            i++;
            for (int idx = 0; idx < 3; ++idx) {
                i = skip_ws(buf, len, i);
                if (i >= len || buf[i] != '"') {
                    return false;
                }
                if (!skip_json_string(buf, len, &i)) {
                    return false;
                }
                i = skip_ws(buf, len, i);
                if (i >= len || buf[i] != ',') {
                    return false;
                }
                i++;
            }
            i = skip_ws(buf, len, i);
            if (i >= len || buf[i] != '"') {
                return false;
            }
            *out_coinb2_start = i + 1U;
            return true;
        }

        if (!skip_json_value(buf, len, &i)) {
            return false;
        }
        i = skip_ws(buf, len, i);
        if (i >= len || buf[i] != ',') {
            return false;
        }
        i++;
    }
    return false;
}

static bool try_begin_coinb2_stream(void)
{
    s_rx_coinb2_checked = true;
    size_t coinb2_start = 0;
    if (!find_params_coinb2_quote(json_rpc_buffer, json_rpc_buffer_len, &coinb2_start)) {
        return true;
    }

    discard_coinb2();
    s_rx_suffix_error = false;
    s_rx_hex_carry = '\0';
    s_rx_coinb2_hex_count = 0;

    char *close_quote = memchr(json_rpc_buffer + coinb2_start, '"',
                               json_rpc_buffer_len - coinb2_start);
    if (close_quote == NULL) {
        size_t hex_len = json_rpc_buffer_len - coinb2_start;
        if (!stream_coinb2_hex_bytes(json_rpc_buffer + coinb2_start, hex_len)) {
            return false;
        }
        json_rpc_buffer[coinb2_start] = '*';
        json_rpc_buffer_len = coinb2_start + 1U;
        json_rpc_buffer[json_rpc_buffer_len] = '\0';
        s_rx_streaming_coinb2 = true;
    } else {
        size_t hex_len = (size_t)(close_quote - (json_rpc_buffer + coinb2_start));
        if (!stream_coinb2_hex_bytes(json_rpc_buffer + coinb2_start, hex_len)) {
            return false;
        }
        if (!s_rx_suffix_error && s_rx_suffix_len > 0 && s_rx_hex_carry == '\0') {
            s_rx_suffix_valid = true;
        } else {
            discard_coinb2();
        }
        size_t tail_len = json_rpc_buffer_len - (size_t)(close_quote - json_rpc_buffer);
        json_rpc_buffer[coinb2_start] = '*';
        memmove(json_rpc_buffer + coinb2_start + 1U, close_quote, tail_len);
        json_rpc_buffer_len = coinb2_start + 1U + tail_len;
        json_rpc_buffer[json_rpc_buffer_len] = '\0';
        s_rx_streaming_coinb2 = false;
    }
    return true;
}

bool stratum_rx_stream_read_line(esp_transport_handle_t transport,
                                 const char **out_line,
                                 size_t *out_line_len,
                                 size_t *out_raw_len)
{
    if (json_rpc_buffer == NULL) {
        if (!stratum_rx_stream_init()) {
            return false;
        }
    }
    reset_rx_line_state();

    char recv_buffer[BUFFER_SIZE];
    int nbytes;

    char *newline_pos = memchr(json_rpc_buffer, '\n', json_rpc_buffer_len);
    while (newline_pos == NULL) {
        if (!s_rx_streaming_coinb2 && !s_rx_coinb2_checked &&
            json_rpc_buffer_len >= STRATUM_V1_COINB2_STREAM_THRESHOLD) {
            if (!try_begin_coinb2_stream()) {
                reset_json_rpc_buffer();
                return false;
            }
        }

        size_t receive_size = sizeof(recv_buffer);
        if (!s_rx_streaming_coinb2) {
            size_t receive_capacity =
                (STRATUM_V1_MAX_JSON_LINE_SIZE + 1U) - json_rpc_buffer_len;
            receive_size = MIN(sizeof(recv_buffer), receive_capacity);
        }

        nbytes = esp_transport_read(transport, recv_buffer, receive_size,
                                    TRANSPORT_TIMEOUT_MS);
        if (nbytes < 0) {
            const char *err_str;
            switch (nbytes) {
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
            reset_json_rpc_buffer();
            return false;
        }
        if (nbytes > 0) {
            if (memchr(recv_buffer, '\0', (size_t)nbytes) != NULL) {
                ESP_LOGE(TAG, "JSON-RPC stream contains an embedded NUL byte");
                reset_json_rpc_buffer();
                return false;
            }

            const char *chunk_ptr = recv_buffer;
            size_t chunk_len = (size_t)nbytes;

            if (s_rx_streaming_coinb2) {
                const char *close_quote = memchr(chunk_ptr, '"', chunk_len);
                if (close_quote == NULL) {
                    if (!stream_coinb2_hex_bytes(chunk_ptr, chunk_len)) {
                        reset_json_rpc_buffer();
                        return false;
                    }
                    continue;
                }
                size_t hex_len = (size_t)(close_quote - chunk_ptr);
                if (!stream_coinb2_hex_bytes(chunk_ptr, hex_len)) {
                    reset_json_rpc_buffer();
                    return false;
                }
                s_rx_streaming_coinb2 = false;
                if (!s_rx_suffix_error && s_rx_suffix_len > 0 && s_rx_hex_carry == '\0') {
                    s_rx_suffix_valid = true;
                } else {
                    discard_coinb2();
                }
                chunk_ptr = close_quote;
                chunk_len -= hex_len;
            }

            size_t required_size = json_rpc_buffer_len + chunk_len + 1U;
            if (!ensure_json_buffer_capacity(required_size)) {
                reset_json_rpc_buffer();
                return false;
            }

            size_t previous_len = json_rpc_buffer_len;
            memcpy(json_rpc_buffer + previous_len, chunk_ptr, chunk_len);
            json_rpc_buffer_len += chunk_len;
            json_rpc_buffer[json_rpc_buffer_len] = '\0';
            newline_pos = memchr(json_rpc_buffer + previous_len, '\n', chunk_len);

            if (newline_pos == NULL && !s_rx_streaming_coinb2 && !s_rx_coinb2_checked &&
                json_rpc_buffer_len >= STRATUM_V1_COINB2_STREAM_THRESHOLD) {
                if (!try_begin_coinb2_stream()) {
                    reset_json_rpc_buffer();
                    return false;
                }
            }

            if (newline_pos == NULL &&
                json_rpc_buffer_len > STRATUM_V1_MAX_JSON_LINE_SIZE) {
                ESP_LOGE(TAG, "JSON-RPC line exceeds %u bytes",
                         STRATUM_V1_MAX_JSON_LINE_SIZE);
                reset_json_rpc_buffer();
                return false;
            }
        }
    }

    size_t line_len = (size_t)(newline_pos - json_rpc_buffer);
    size_t raw_len = line_len + (s_rx_coinb2_hex_count > 0 ? (s_rx_coinb2_hex_count - 1U) : 0U);
    json_rpc_buffer[line_len] = '\0';
    if (out_line != NULL) {
        *out_line = json_rpc_buffer;
    }
    if (out_line_len != NULL) {
        *out_line_len = line_len;
    }
    if (out_raw_len != NULL) {
        *out_raw_len = raw_len;
    }
    return true;
}

void stratum_rx_stream_consume_line(size_t line_len)
{
    discard_coinb2();
    if (json_rpc_buffer == NULL || line_len >= json_rpc_buffer_len) {
        json_rpc_buffer_len = 0;
        if (json_rpc_buffer != NULL) {
            json_rpc_buffer[0] = '\0';
        }
        return;
    }
    size_t remaining_len = json_rpc_buffer_len - line_len - 1U;
    if (remaining_len > 0) {
        memmove(json_rpc_buffer, json_rpc_buffer + line_len + 1U, remaining_len);
    }
    json_rpc_buffer_len = remaining_len;
    json_rpc_buffer[json_rpc_buffer_len] = '\0';
}

bool stratum_rx_stream_has_coinb2(const char *c2_str, size_t *out_len)
{
    if (!s_rx_suffix_valid || c2_str == NULL || c2_str[0] != '*' || c2_str[1] != '\0') {
        return false;
    }
    if (out_len != NULL) {
        *out_len = s_rx_suffix_len;
    }
    return true;
}

uint8_t *stratum_rx_stream_take_coinb2(size_t *out_len)
{
    if (!s_rx_suffix_valid || s_rx_suffix == NULL) {
        return NULL;
    }
    size_t len = s_rx_suffix_len;
    uint32_t caps = MALLOC_CAP_8BIT;
    if (esp_psram_is_initialized()) caps |= MALLOC_CAP_SPIRAM;
    uint8_t *shrunk = heap_caps_realloc(s_rx_suffix, len, caps);
    if (shrunk != NULL) {
        s_rx_suffix = shrunk;
    }
    uint8_t *result = s_rx_suffix;
    s_rx_suffix = NULL;
    s_rx_suffix_capacity = 0;
    s_rx_suffix_len = 0;
    s_rx_suffix_valid = false;
    if (out_len != NULL) {
        *out_len = len;
    }
    return result;
}

