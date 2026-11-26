#include "stratum_rx_stream.h"
#include "stratum_api.h"
#include "stratum_socket.h"
#include "utils.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#define INITIAL_JSON_BUFFER_SIZE 1024U
#define RX_READ_BUFFER_SIZE 16384U
#define INITIAL_COINB2_CAPACITY 32768U
#define JSON_RPC_BUFFER_LIMIT (STRATUM_V1_MAX_JSON_LINE_SIZE + 2U)
#define STRATUM_V1_COINB2_STREAM_THRESHOLD 4096U

static const char *TAG = "stratum_rx_stream";

typedef struct {
    size_t suffix_capacity;
    size_t coinb2_hex_count;
    bool streaming_coinb2;
    bool coinb2_checked;
    bool suffix_error;
    char hex_carry;
} rx_line_state_t;

static void discard_coinb2(stratum_rx_stream_t *stream)
{
    free(stream->suffix);
    stream->suffix = NULL;
    stream->suffix_len = 0;
    stream->suffix_valid = false;
}

static void reset_json_rpc_buffer(stratum_rx_stream_t *stream)
{
    discard_coinb2(stream);
    stream->line_start_us = 0;
    stream->json_rpc_buffer_len = 0;
    if (stream->json_rpc_buffer != NULL) {
        stream->json_rpc_buffer[0] = '\0';
    }
}

void stratum_rx_stream_free(stratum_rx_stream_t *stream)
{
    if (stream == NULL) {
        return;
    }
    discard_coinb2(stream);
    stream->line_start_us = 0;
    free(stream->json_rpc_buffer);
    stream->json_rpc_buffer = NULL;
    free(stream->rx_read_buffer);
    stream->rx_read_buffer = NULL;
    stream->json_rpc_buffer_size = 0;
    stream->json_rpc_buffer_len = 0;
}

bool stratum_rx_stream_init(stratum_rx_stream_t *stream)
{
    if (stream == NULL) {
        return false;
    }
    stratum_rx_stream_free(stream);

    stream->json_rpc_buffer = heap_caps_malloc(INITIAL_JSON_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (stream->json_rpc_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for JSON-RPC buffer");
        return false;
    }
    stream->rx_read_buffer = heap_caps_malloc(RX_READ_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (stream->rx_read_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for Stratum RX chunk buffer");
        free(stream->json_rpc_buffer);
        stream->json_rpc_buffer = NULL;
        return false;
    }
    stream->json_rpc_buffer_size = INITIAL_JSON_BUFFER_SIZE;
    stream->json_rpc_buffer[0] = '\0';

    return true;
}

static bool ensure_json_buffer_capacity(stratum_rx_stream_t *stream, size_t required_size)
{
    if (required_size > JSON_RPC_BUFFER_LIMIT) {
        return false;
    }

    if (required_size <= stream->json_rpc_buffer_size) {
        return true;
    }

    size_t new_size = stream->json_rpc_buffer_size;
    while (new_size < required_size && new_size < JSON_RPC_BUFFER_LIMIT) {
        new_size = MIN(new_size * 2U, JSON_RPC_BUFFER_LIMIT);
    }

    char *new_buffer = heap_caps_realloc(stream->json_rpc_buffer, new_size,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (new_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to grow JSON-RPC receive buffer to %zu bytes", new_size);
        return false;
    }

    stream->json_rpc_buffer = new_buffer;
    stream->json_rpc_buffer_size = new_size;
    return true;
}

static bool stream_coinb2_hex_bytes(stratum_rx_stream_t *stream, rx_line_state_t *line,
                                    const char *data, size_t len)
{
    if (len == 0) {
        return true;
    }
    if (len > (size_t)MAX_COINBASE_SUFFIX_LEN * 2U - line->coinb2_hex_count) {
        ESP_LOGE(TAG, "Streamed coinbase_2 exceeds maximum suffix size (%u bytes)",
                 MAX_COINBASE_SUFFIX_LEN);
        return false;
    }
    line->coinb2_hex_count += len;

    if (line->suffix_error) {
        return true;
    }

    size_t total_hex = (line->hex_carry != '\0' ? 1U : 0U) + len;
    size_t new_bytes = total_hex / 2U;
    if (stream->suffix_len + new_bytes > line->suffix_capacity) {
        size_t new_cap = line->suffix_capacity ? line->suffix_capacity : INITIAL_COINB2_CAPACITY;
        while (new_cap < stream->suffix_len + new_bytes && new_cap < MAX_COINBASE_SUFFIX_LEN) {
            new_cap = MIN(new_cap * 2U, (size_t)MAX_COINBASE_SUFFIX_LEN);
        }
        uint8_t *new_buf = heap_caps_realloc(stream->suffix, new_cap,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!new_buf) {
            ESP_LOGE(TAG, "Failed to grow streamed coinbase suffix buffer to %zu bytes", new_cap);
            return false;
        }
        stream->suffix = new_buf;
        line->suffix_capacity = new_cap;
    }

    size_t idx = 0;
    if (line->hex_carry != '\0') {
        char pair[3] = {line->hex_carry, data[0], '\0'};
        line->hex_carry = '\0';
        if (hex2bin(pair, stream->suffix + stream->suffix_len, 1U) != 1U) {
            ESP_LOGE(TAG, "Invalid hex character in streamed coinbase_2");
            line->suffix_error = true;
            return true;
        }
        stream->suffix_len++;
        idx = 1U;
    }

    size_t bulk_bytes = (len - idx) / 2U;
    if (bulk_bytes > 0) {
        if (hex2bin(data + idx, stream->suffix + stream->suffix_len, bulk_bytes) != bulk_bytes) {
            ESP_LOGE(TAG, "Invalid hex character in streamed coinbase_2");
            line->suffix_error = true;
            return true;
        }
        stream->suffix_len += bulk_bytes;
        idx += bulk_bytes * 2U;
    }

    if (idx < len) {
        if (hex_val_table[(unsigned char)data[idx]] < 0) {
            ESP_LOGE(TAG, "Invalid hex character in streamed coinbase_2");
            line->suffix_error = true;
            return true;
        }
        line->hex_carry = data[idx];
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

static bool try_begin_coinb2_stream(stratum_rx_stream_t *stream, rx_line_state_t *line,
                                    size_t parse_len)
{
    line->coinb2_checked = true;
    size_t coinb2_start = 0;
    if (!find_params_coinb2_quote(stream->json_rpc_buffer, parse_len, &coinb2_start)) {
        return true;
    }

    discard_coinb2(stream);
    line->suffix_capacity = 0;
    line->suffix_error = false;
    line->hex_carry = '\0';
    line->coinb2_hex_count = 0;

    char *close_quote = memchr(stream->json_rpc_buffer + coinb2_start, '"',
                               parse_len - coinb2_start);
    if (close_quote == NULL) {
        size_t hex_len = parse_len - coinb2_start;
        if (!stream_coinb2_hex_bytes(stream, line, stream->json_rpc_buffer + coinb2_start, hex_len)) {
            return false;
        }
        stream->json_rpc_buffer[coinb2_start] = '*';
        stream->json_rpc_buffer_len = coinb2_start + 1U;
        stream->json_rpc_buffer[stream->json_rpc_buffer_len] = '\0';
        line->streaming_coinb2 = true;
    } else {
        size_t hex_len = (size_t)(close_quote - (stream->json_rpc_buffer + coinb2_start));
        if (!stream_coinb2_hex_bytes(stream, line, stream->json_rpc_buffer + coinb2_start, hex_len)) {
            return false;
        }
        if (!line->suffix_error && stream->suffix_len > 0 && line->hex_carry == '\0') {
            stream->suffix_valid = true;
        } else {
            discard_coinb2(stream);
            line->suffix_capacity = 0;
        }
        size_t tail_len = stream->json_rpc_buffer_len - (size_t)(close_quote - stream->json_rpc_buffer);
        stream->json_rpc_buffer[coinb2_start] = '*';
        memmove(stream->json_rpc_buffer + coinb2_start + 1U, close_quote, tail_len);
        stream->json_rpc_buffer_len = coinb2_start + 1U + tail_len;
        stream->json_rpc_buffer[stream->json_rpc_buffer_len] = '\0';
        line->streaming_coinb2 = false;
    }
    return true;
}

bool stratum_rx_stream_read_line(stratum_rx_stream_t *stream,
                                 esp_transport_handle_t transport,
                                 const char **out_line,
                                 size_t *out_line_len,
                                 size_t *out_raw_len)
{
    if (stream == NULL || transport == NULL) {
        return false;
    }
    if (stream->json_rpc_buffer == NULL || stream->rx_read_buffer == NULL) {
        if (!stratum_rx_stream_init(stream)) {
            return false;
        }
    }
    discard_coinb2(stream);
    stream->line_start_us = (stream->json_rpc_buffer_len > 0) ? esp_timer_get_time() : 0;
    rx_line_state_t line = {0};

    int nbytes;

    char *newline_pos = memchr(stream->json_rpc_buffer, '\n', stream->json_rpc_buffer_len);
    while (newline_pos == NULL) {
        if (!line.streaming_coinb2 && !line.coinb2_checked &&
            stream->json_rpc_buffer_len >= STRATUM_V1_COINB2_STREAM_THRESHOLD) {
            if (!try_begin_coinb2_stream(stream, &line, stream->json_rpc_buffer_len)) {
                reset_json_rpc_buffer(stream);
                return false;
            }
        }

        size_t receive_capacity =
            (STRATUM_V1_MAX_JSON_LINE_SIZE + 1U) - stream->json_rpc_buffer_len;
        size_t receive_size = MIN((size_t)RX_READ_BUFFER_SIZE, receive_capacity);

        nbytes = esp_transport_read(transport, stream->rx_read_buffer, receive_size,
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
            reset_json_rpc_buffer(stream);
            return false;
        }
        if (nbytes > 0) {
            if (stream->line_start_us == 0) {
                stream->line_start_us = esp_timer_get_time();
            }
            if (memchr(stream->rx_read_buffer, '\0', (size_t)nbytes) != NULL) {
                ESP_LOGE(TAG, "JSON-RPC stream contains an embedded NUL byte");
                reset_json_rpc_buffer(stream);
                return false;
            }

            const char *chunk_ptr = stream->rx_read_buffer;
            size_t chunk_len = (size_t)nbytes;

            if (line.streaming_coinb2) {
                const char *close_quote = memchr(chunk_ptr, '"', chunk_len);
                if (close_quote == NULL) {
                    if (!stream_coinb2_hex_bytes(stream, &line, chunk_ptr, chunk_len)) {
                        reset_json_rpc_buffer(stream);
                        return false;
                    }
                    continue;
                }
                size_t hex_len = (size_t)(close_quote - chunk_ptr);
                if (!stream_coinb2_hex_bytes(stream, &line, chunk_ptr, hex_len)) {
                    reset_json_rpc_buffer(stream);
                    return false;
                }
                line.streaming_coinb2 = false;
                if (!line.suffix_error && stream->suffix_len > 0 && line.hex_carry == '\0') {
                    stream->suffix_valid = true;
                } else {
                    discard_coinb2(stream);
                    line.suffix_capacity = 0;
                }
                chunk_ptr = close_quote;
                chunk_len -= hex_len;
            }

            size_t required_size = stream->json_rpc_buffer_len + chunk_len + 1U;
            if (!ensure_json_buffer_capacity(stream, required_size)) {
                reset_json_rpc_buffer(stream);
                return false;
            }

            size_t previous_len = stream->json_rpc_buffer_len;
            memcpy(stream->json_rpc_buffer + previous_len, chunk_ptr, chunk_len);
            stream->json_rpc_buffer_len += chunk_len;
            stream->json_rpc_buffer[stream->json_rpc_buffer_len] = '\0';
            newline_pos = memchr(stream->json_rpc_buffer + previous_len, '\n', chunk_len);

            if (newline_pos == NULL && !line.streaming_coinb2 && !line.coinb2_checked &&
                stream->json_rpc_buffer_len >= STRATUM_V1_COINB2_STREAM_THRESHOLD) {
                if (!try_begin_coinb2_stream(stream, &line, stream->json_rpc_buffer_len)) {
                    reset_json_rpc_buffer(stream);
                    return false;
                }
            }

            if (newline_pos == NULL &&
                stream->json_rpc_buffer_len > STRATUM_V1_MAX_JSON_LINE_SIZE) {
                ESP_LOGE(TAG, "JSON-RPC line exceeds %u bytes",
                         STRATUM_V1_MAX_JSON_LINE_SIZE);
                reset_json_rpc_buffer(stream);
                return false;
            }
        }
    }

    size_t line_len = (size_t)(newline_pos - stream->json_rpc_buffer);
    if (!line.streaming_coinb2 && !line.coinb2_checked &&
        line_len >= STRATUM_V1_COINB2_STREAM_THRESHOLD) {
        if (!try_begin_coinb2_stream(stream, &line, line_len)) {
            reset_json_rpc_buffer(stream);
            return false;
        }
        newline_pos = memchr(stream->json_rpc_buffer, '\n', stream->json_rpc_buffer_len);
        line_len = (size_t)(newline_pos - stream->json_rpc_buffer);
    }
    if (line_len > STRATUM_V1_MAX_JSON_LINE_SIZE) {
        ESP_LOGE(TAG, "JSON-RPC line exceeds %u bytes",
                 STRATUM_V1_MAX_JSON_LINE_SIZE);
        reset_json_rpc_buffer(stream);
        return false;
    }
    size_t raw_len = line_len + (line.coinb2_hex_count > 0 ? (line.coinb2_hex_count - 1U) : 0U);
    stream->json_rpc_buffer[line_len] = '\0';
    if (out_line != NULL) {
        *out_line = stream->json_rpc_buffer;
    }
    if (out_line_len != NULL) {
        *out_line_len = line_len;
    }
    if (out_raw_len != NULL) {
        *out_raw_len = raw_len;
    }
    return true;
}

void stratum_rx_stream_consume_line(stratum_rx_stream_t *stream, size_t line_len)
{
    if (stream == NULL) {
        return;
    }
    discard_coinb2(stream);
    if (stream->json_rpc_buffer == NULL || line_len >= stream->json_rpc_buffer_len) {
        stream->json_rpc_buffer_len = 0;
        if (stream->json_rpc_buffer != NULL) {
            stream->json_rpc_buffer[0] = '\0';
        }
        return;
    }
    size_t remaining_len = stream->json_rpc_buffer_len - line_len - 1U;
    if (remaining_len > 0) {
        memmove(stream->json_rpc_buffer, stream->json_rpc_buffer + line_len + 1U, remaining_len);
    }
    stream->json_rpc_buffer_len = remaining_len;
    stream->json_rpc_buffer[stream->json_rpc_buffer_len] = '\0';
}

bool stratum_rx_stream_has_coinb2(const stratum_rx_stream_t *stream,
                                  const char *c2_str,
                                  size_t *out_len)
{
    if (stream == NULL || !stream->suffix_valid || c2_str == NULL ||
        c2_str[0] != '*' || c2_str[1] != '\0') {
        return false;
    }
    if (out_len != NULL) {
        *out_len = stream->suffix_len;
    }
    return true;
}

uint8_t *stratum_rx_stream_take_coinb2(stratum_rx_stream_t *stream, size_t *out_len)
{
    if (stream == NULL || !stream->suffix_valid || stream->suffix == NULL) {
        return NULL;
    }
    size_t len = stream->suffix_len;
    uint8_t *shrunk = heap_caps_realloc(stream->suffix, len,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (shrunk != NULL) {
        stream->suffix = shrunk;
    }
    uint8_t *result = stream->suffix;
    stream->suffix = NULL;
    stream->suffix_len = 0;
    stream->suffix_valid = false;
    if (out_len != NULL) {
        *out_len = len;
    }
    return result;
}

int64_t stratum_rx_stream_line_start_us(const stratum_rx_stream_t *stream)
{
    return (stream != NULL) ? stream->line_start_us : 0;
}
