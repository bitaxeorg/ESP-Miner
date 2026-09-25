#include "unity.h"
#include "sv1_client.h"
#include "sv1_protocol.h"

#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

typedef struct {
    const char *data;
    size_t length;
    size_t offset;
    size_t max_chunk;
} mock_transport_data_t;

static int mock_transport_read(esp_transport_handle_t transport, char *buffer,
                               int len, int timeout_ms)
{
    mock_transport_data_t *mock =
        (mock_transport_data_t *)esp_transport_get_context_data(transport);
    if (mock == NULL || mock->offset >= mock->length) {
        return 0;
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

static esp_transport_handle_t create_mock_transport(mock_transport_data_t *data)
{
    esp_transport_handle_t transport = esp_transport_init();
    if (transport == NULL ||
        esp_transport_set_context_data(transport, data) != ESP_OK ||
        esp_transport_set_func(transport, NULL, mock_transport_read, NULL,
                               NULL, NULL, NULL, NULL) != ESP_OK) {
        esp_transport_destroy(transport);
        return NULL;
    }
    return transport;
}

TEST_CASE("Receive fragmented JSON-RPC line", "[stratum][security][qemu-integration]")
{
    const char *json = "{\"id\":1,\"result\":true,\"error\":null}\n";
    mock_transport_data_t mock = {
        .data = json,
        .length = strlen(json),
        .max_chunk = 3,
    };
    esp_transport_handle_t transport = create_mock_transport(&mock);
    TEST_ASSERT_NOT_NULL(transport);

    TEST_ASSERT_TRUE(STRATUM_V1_initialize_buffer());
    char *line = STRATUM_V1_receive_jsonrpc_line(transport);
    TEST_ASSERT_NOT_NULL(line);
    TEST_ASSERT_EQUAL_STRING("{\"id\":1,\"result\":true,\"error\":null}", line);

    free(line);
    esp_transport_destroy(transport);
}

TEST_CASE("Receive preserves consecutive JSON-RPC lines", "[stratum][security][qemu-integration]")
{
    const char *json =
        "{\"id\":1,\"result\":true}\n"
        "{\"id\":2,\"result\":false}\n";
    mock_transport_data_t mock = {
        .data = json,
        .length = strlen(json),
        .max_chunk = strlen(json),
    };
    esp_transport_handle_t transport = create_mock_transport(&mock);
    TEST_ASSERT_NOT_NULL(transport);

    TEST_ASSERT_TRUE(STRATUM_V1_initialize_buffer());
    char *first = STRATUM_V1_receive_jsonrpc_line(transport);
    char *second = STRATUM_V1_receive_jsonrpc_line(transport);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_EQUAL_STRING("{\"id\":1,\"result\":true}", first);
    TEST_ASSERT_EQUAL_STRING("{\"id\":2,\"result\":false}", second);

    free(first);
    free(second);
    esp_transport_destroy(transport);
}

TEST_CASE("Receive rejects oversized JSON-RPC line", "[stratum][security][qemu-integration]")
{
    size_t data_length = STRATUM_V1_MAX_JSON_LINE_SIZE + 2U;
    char *json = malloc(data_length);
    TEST_ASSERT_NOT_NULL(json);
    memset(json, 'a', data_length - 1U);
    json[data_length - 1U] = '\n';

    mock_transport_data_t mock = {
        .data = json,
        .length = data_length,
        .max_chunk = 1024,
    };
    esp_transport_handle_t transport = create_mock_transport(&mock);
    TEST_ASSERT_NOT_NULL(transport);

    TEST_ASSERT_TRUE(STRATUM_V1_initialize_buffer());
    TEST_ASSERT_NULL(STRATUM_V1_receive_jsonrpc_line(transport));

    esp_transport_destroy(transport);
    free(json);
}

TEST_CASE("Receive accepts maximum line and preserves the next line", "[stratum][security][qemu-integration]")
{
    const char *next_line = "{}\n";
    size_t data_length = STRATUM_V1_MAX_JSON_LINE_SIZE + 1U + strlen(next_line);
    char *data = malloc(data_length);
    TEST_ASSERT_NOT_NULL(data);
    memset(data, 'a', STRATUM_V1_MAX_JSON_LINE_SIZE);
    data[STRATUM_V1_MAX_JSON_LINE_SIZE] = '\n';
    memcpy(data + STRATUM_V1_MAX_JSON_LINE_SIZE + 1U, next_line, strlen(next_line));

    mock_transport_data_t mock = {
        .data = data,
        .length = data_length,
        .max_chunk = data_length,
    };
    esp_transport_handle_t transport = create_mock_transport(&mock);
    TEST_ASSERT_NOT_NULL(transport);

    TEST_ASSERT_TRUE(STRATUM_V1_initialize_buffer());
    char *maximum_line = STRATUM_V1_receive_jsonrpc_line(transport);
    char *second_line = STRATUM_V1_receive_jsonrpc_line(transport);
    TEST_ASSERT_NOT_NULL(maximum_line);
    TEST_ASSERT_EQUAL_size_t(STRATUM_V1_MAX_JSON_LINE_SIZE, strlen(maximum_line));
    TEST_ASSERT_EQUAL_STRING("{}", second_line);

    free(maximum_line);
    free(second_line);
    esp_transport_destroy(transport);
    free(data);
}

TEST_CASE("Receive rejects embedded NUL", "[stratum][security][qemu-integration]")
{
    const char data[] = {'{', '}', '\0', '\n'};
    mock_transport_data_t mock = {
        .data = data,
        .length = sizeof(data),
        .max_chunk = sizeof(data),
    };
    esp_transport_handle_t transport = create_mock_transport(&mock);
    TEST_ASSERT_NOT_NULL(transport);

    TEST_ASSERT_TRUE(STRATUM_V1_initialize_buffer());
    TEST_ASSERT_NULL(STRATUM_V1_receive_jsonrpc_line(transport));

    esp_transport_destroy(transport);
}
