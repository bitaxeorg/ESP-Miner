#include "unity.h"
#include "stratum_rx_stream.h"
#include "stratum_api.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

typedef struct {
    const char *data;
    size_t length;
    size_t offset;
    size_t max_chunk;
} stream_mock_transport_t;

static int stream_mock_read(esp_transport_handle_t transport, char *buffer,
                            int len, int timeout_ms)
{
    (void)timeout_ms;
    stream_mock_transport_t *mock =
        (stream_mock_transport_t *)esp_transport_get_context_data(transport);
    if (mock == NULL || mock->offset >= mock->length) {
        return ERR_TCP_TRANSPORT_CONNECTION_CLOSED_BY_FIN;
    }

    size_t remaining = mock->length - mock->offset;
    size_t bytes_to_copy = MIN((size_t)len, remaining);
    if (mock->max_chunk > 0 && bytes_to_copy > mock->max_chunk) {
        bytes_to_copy = mock->max_chunk;
    }

    memcpy(buffer, mock->data + mock->offset, bytes_to_copy);
    mock->offset += bytes_to_copy;
    return (int)bytes_to_copy;
}

static esp_transport_handle_t create_stream_mock_transport(stream_mock_transport_t *data)
{
    esp_transport_handle_t transport = esp_transport_init();
    if (transport == NULL ||
        esp_transport_set_context_data(transport, data) != ESP_OK ||
        esp_transport_set_func(transport, NULL, stream_mock_read, NULL,
                               NULL, NULL, NULL, NULL) != ESP_OK) {
        esp_transport_destroy(transport);
        return NULL;
    }
    return transport;
}

TEST_CASE("stratum_rx_stream splices large coinb2 across odd chunk boundaries", "[stratum][rx_stream]")
{
    const size_t suffix_len = 2500U;
    uint8_t *expected_suffix = malloc(suffix_len);
    TEST_ASSERT_NOT_NULL(expected_suffix);
    for (size_t i = 0; i < suffix_len; ++i) {
        expected_suffix[i] = (uint8_t)((i * 73U + 19U) & 0xFFU);
    }

    size_t json_cap = suffix_len * 2U + 512U;
    char *json = malloc(json_cap);
    TEST_ASSERT_NOT_NULL(json);
    size_t offset = (size_t)snprintf(
        json, json_cap,
        "{\"id\":null,\"method\":\"mining.notify\",\"params\":[\"job42\","
        "\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"01000000\",\"");
    bin2hex(expected_suffix, suffix_len, json + offset, json_cap - offset);
    offset += suffix_len * 2U;
    offset += (size_t)snprintf(
        json + offset, json_cap - offset,
        "\",[],\"20000000\",\"1705ae3a\",\"647025b5\",true]}\n");

    stream_mock_transport_t mock = {
        .data = json,
        .length = offset,
        .max_chunk = 37, // Odd chunk size exercises hex carry across chunks
    };
    esp_transport_handle_t transport = create_stream_mock_transport(&mock);
    TEST_ASSERT_NOT_NULL(transport);
    TEST_ASSERT_TRUE(stratum_rx_stream_init());

    const char *line_buf = NULL;
    size_t line_len = 0;
    size_t raw_len = 0;
    TEST_ASSERT_TRUE(stratum_rx_stream_read_line(transport, &line_buf, &line_len, &raw_len));
    TEST_ASSERT_NOT_NULL(line_buf);
    TEST_ASSERT_EQUAL_size_t(offset - 1U, raw_len);
    TEST_ASSERT_NOT_NULL(strstr(line_buf, "\"01000000\",\"*\",[]"));

    size_t c2_len = 0;
    TEST_ASSERT_FALSE(stratum_rx_stream_has_coinb2("0102", &c2_len));
    TEST_ASSERT_TRUE(stratum_rx_stream_has_coinb2("*", &c2_len));
    TEST_ASSERT_EQUAL_size_t(suffix_len, c2_len);

    size_t taken_len = 0;
    uint8_t *taken = stratum_rx_stream_take_coinb2(&taken_len);
    TEST_ASSERT_NOT_NULL(taken);
    TEST_ASSERT_EQUAL_size_t(suffix_len, taken_len);
    TEST_ASSERT_EQUAL_MEMORY(expected_suffix, taken, suffix_len);
    TEST_ASSERT_FALSE(stratum_rx_stream_has_coinb2("*", NULL));
    TEST_ASSERT_NULL(stratum_rx_stream_take_coinb2(NULL));

    stratum_rx_stream_consume_line(line_len);
    free(taken);
    free(json);
    free(expected_suffix);
    esp_transport_destroy(transport);
}

TEST_CASE("stratum_rx_stream supports params before method and discards unconsumed coinb2", "[stratum][rx_stream]")
{
    const size_t suffix_len = 2200U;
    size_t json_cap = suffix_len * 2U + 512U;
    char *json = malloc(json_cap);
    TEST_ASSERT_NOT_NULL(json);

    size_t offset = (size_t)snprintf(
        json, json_cap,
        "{\"params\":[\"job99\","
        "\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"01000000\",\"");
    memset(json + offset, 'a', suffix_len * 2U);
    offset += suffix_len * 2U;
    offset += (size_t)snprintf(
        json + offset, json_cap - offset,
        "\",[],\"20000000\",\"1705ae3a\",\"647025b5\",true],"
        "\"method\":\"mining.notify\",\"id\":null}\n{\"id\":2,\"result\":true}\n");

    stream_mock_transport_t mock = {
        .data = json,
        .length = offset,
        .max_chunk = 509,
    };
    esp_transport_handle_t transport = create_stream_mock_transport(&mock);
    TEST_ASSERT_NOT_NULL(transport);
    TEST_ASSERT_TRUE(stratum_rx_stream_init());

    const char *line_buf = NULL;
    size_t line_len = 0;
    TEST_ASSERT_TRUE(stratum_rx_stream_read_line(transport, &line_buf, &line_len, NULL));
    size_t c2_len = 0;
    TEST_ASSERT_TRUE(stratum_rx_stream_has_coinb2("*", &c2_len));
    TEST_ASSERT_EQUAL_size_t(suffix_len, c2_len);

    // Consuming without taking coinb2 must automatically discard the buffer
    stratum_rx_stream_consume_line(line_len);
    TEST_ASSERT_FALSE(stratum_rx_stream_has_coinb2("*", NULL));
    TEST_ASSERT_NULL(stratum_rx_stream_take_coinb2(NULL));

    TEST_ASSERT_TRUE(stratum_rx_stream_read_line(transport, &line_buf, &line_len, NULL));
    TEST_ASSERT_EQUAL_STRING("{\"id\":2,\"result\":true}", line_buf);
    stratum_rx_stream_consume_line(line_len);

    free(json);
    esp_transport_destroy(transport);
}

TEST_CASE("stratum_rx_stream invalidates streamed coinb2 on odd length or invalid hex", "[stratum][rx_stream]")
{
    const size_t hex_len = 4400U;
    size_t json_cap = hex_len + 512U;
    char *json = malloc(json_cap);
    TEST_ASSERT_NOT_NULL(json);

    // 1. Odd number of hex characters (4401 chars)
    size_t offset = (size_t)snprintf(
        json, json_cap,
        "{\"id\":null,\"method\":\"mining.notify\",\"params\":[\"job_odd\","
        "\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"01000000\",\"");
    memset(json + offset, 'b', hex_len + 1U);
    offset += hex_len + 1U;
    offset += (size_t)snprintf(
        json + offset, json_cap - offset,
        "\",[],\"20000000\",\"1705ae3a\",\"647025b5\",true]}\n");

    stream_mock_transport_t mock = {
        .data = json,
        .length = offset,
        .max_chunk = 251,
    };
    esp_transport_handle_t transport = create_stream_mock_transport(&mock);
    TEST_ASSERT_NOT_NULL(transport);
    TEST_ASSERT_TRUE(stratum_rx_stream_init());

    const char *line_buf = NULL;
    size_t line_len = 0;
    TEST_ASSERT_TRUE(stratum_rx_stream_read_line(transport, &line_buf, &line_len, NULL));
    TEST_ASSERT_FALSE(stratum_rx_stream_has_coinb2("*", NULL));
    TEST_ASSERT_NULL(stratum_rx_stream_take_coinb2(NULL));
    stratum_rx_stream_consume_line(line_len);
    esp_transport_destroy(transport);

    // 2. Invalid hex character ('z') in even-length streamed coinb2
    offset = (size_t)snprintf(
        json, json_cap,
        "{\"id\":null,\"method\":\"mining.notify\",\"params\":[\"job_badhex\","
        "\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"01000000\",\"");
    memset(json + offset, 'c', hex_len);
    json[offset + hex_len - 10U] = 'z';
    offset += hex_len;
    offset += (size_t)snprintf(
        json + offset, json_cap - offset,
        "\",[],\"20000000\",\"1705ae3a\",\"647025b5\",true]}\n");

    mock.data = json;
    mock.length = offset;
    mock.offset = 0;
    transport = create_stream_mock_transport(&mock);
    TEST_ASSERT_NOT_NULL(transport);

    TEST_ASSERT_TRUE(stratum_rx_stream_read_line(transport, &line_buf, &line_len, NULL));
    TEST_ASSERT_FALSE(stratum_rx_stream_has_coinb2("*", NULL));
    TEST_ASSERT_NULL(stratum_rx_stream_take_coinb2(NULL));
    stratum_rx_stream_consume_line(line_len);

    free(json);
    esp_transport_destroy(transport);
}

TEST_CASE("stratum_rx_stream rejects oversized non-notify method", "[stratum][rx_stream]")
{
    const size_t payload_len = STRATUM_V1_MAX_JSON_LINE_SIZE + 512U;
    size_t json_cap = payload_len + 256U;
    char *json = malloc(json_cap);
    TEST_ASSERT_NOT_NULL(json);

    size_t offset = (size_t)snprintf(
        json, json_cap,
        "{\"id\":null,\"method\":\"client.show_message\",\"params\":[\"a\",\"b\",\"c\",\"");
    memset(json + offset, 'd', payload_len);
    offset += payload_len;
    offset += (size_t)snprintf(json + offset, json_cap - offset, "\"]}\n");

    stream_mock_transport_t mock = {
        .data = json,
        .length = offset,
        .max_chunk = 512,
    };
    esp_transport_handle_t transport = create_stream_mock_transport(&mock);
    TEST_ASSERT_NOT_NULL(transport);
    TEST_ASSERT_TRUE(stratum_rx_stream_init());

    const char *line_buf = NULL;
    size_t line_len = 0;
    TEST_ASSERT_FALSE(stratum_rx_stream_read_line(transport, &line_buf, &line_len, NULL));

    free(json);
    esp_transport_destroy(transport);
}

typedef struct {
    const char *prefix;
    size_t prefix_len;
    size_t hex_len;
    const char *suffix;
    size_t suffix_len;
    size_t offset;
} generated_notify_transport_t;

static int generated_notify_read(esp_transport_handle_t transport, char *buffer,
                                 int len, int timeout_ms)
{
    (void)timeout_ms;
    generated_notify_transport_t *gen =
        (generated_notify_transport_t *)esp_transport_get_context_data(transport);
    size_t total = gen->prefix_len + gen->hex_len + gen->suffix_len;
    if (gen == NULL || gen->offset >= total) {
        return ERR_TCP_TRANSPORT_CONNECTION_CLOSED_BY_FIN;
    }

    size_t max_copy = MIN((size_t)len, 733U);
    size_t copied = 0;
    while (copied < max_copy && gen->offset < total) {
        if (gen->offset < gen->prefix_len) {
            size_t avail = MIN(max_copy - copied, gen->prefix_len - gen->offset);
            memcpy(buffer + copied, gen->prefix + gen->offset, avail);
            copied += avail;
            gen->offset += avail;
        } else if (gen->offset < gen->prefix_len + gen->hex_len) {
            size_t avail = MIN(max_copy - copied, (gen->prefix_len + gen->hex_len) - gen->offset);
            memset(buffer + copied, 'a', avail);
            copied += avail;
            gen->offset += avail;
        } else {
            size_t suf_off = gen->offset - (gen->prefix_len + gen->hex_len);
            size_t avail = MIN(max_copy - copied, gen->suffix_len - suf_off);
            memcpy(buffer + copied, gen->suffix + suf_off, avail);
            copied += avail;
            gen->offset += avail;
        }
    }
    return (int)copied;
}

TEST_CASE("stratum_rx_stream rejects coinb2 exceeding MAX_COINBASE_SUFFIX_LEN", "[stratum][rx_stream]")
{
    const char *prefix =
        "{\"id\":null,\"method\":\"mining.notify\",\"params\":[\"oversized\","
        "\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"01000000\",\"";
    const char *suffix = "\",[],\"20000000\",\"1705ae3a\",\"647025b5\",true]}\n";

    generated_notify_transport_t gen = {
        .prefix = prefix,
        .prefix_len = strlen(prefix),
        .hex_len = ((size_t)MAX_COINBASE_SUFFIX_LEN + 1U) * 2U,
        .suffix = suffix,
        .suffix_len = strlen(suffix),
        .offset = 0,
    };

    esp_transport_handle_t transport = esp_transport_init();
    TEST_ASSERT_NOT_NULL(transport);
    TEST_ASSERT_EQUAL(ESP_OK, esp_transport_set_context_data(transport, &gen));
    TEST_ASSERT_EQUAL(ESP_OK, esp_transport_set_func(transport, NULL, generated_notify_read,
                                                     NULL, NULL, NULL, NULL, NULL));
    TEST_ASSERT_TRUE(stratum_rx_stream_init());

    const char *line_buf = NULL;
    size_t line_len = 0;
    TEST_ASSERT_FALSE(stratum_rx_stream_read_line(transport, &line_buf, &line_len, NULL));
    TEST_ASSERT_FALSE(stratum_rx_stream_has_coinb2("*", NULL));
    TEST_ASSERT_NULL(stratum_rx_stream_take_coinb2(NULL));

    esp_transport_destroy(transport);
}

