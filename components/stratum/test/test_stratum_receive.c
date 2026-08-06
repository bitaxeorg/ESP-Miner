#include <stdlib.h>
#include <string.h>

#include "esp_transport.h"
#include "stratum_api.h"
#include "unity.h"

typedef struct
{
    const char * data;
    size_t length;
    size_t offset;
    size_t max_chunk;
} mock_transport_data_t;

static int mock_transport_read(esp_transport_handle_t transport, char * buffer, int len, int timeout_ms)
{
    (void) timeout_ms;
    mock_transport_data_t * mock = esp_transport_get_context_data(transport);
    if (mock->offset >= mock->length) {
        return ERR_TCP_TRANSPORT_CONNECTION_CLOSED_BY_FIN;
    }

    size_t bytes_to_copy = mock->length - mock->offset;
    if (bytes_to_copy > (size_t) len) {
        bytes_to_copy = (size_t) len;
    }
    if (mock->max_chunk > 0 && bytes_to_copy > mock->max_chunk) {
        bytes_to_copy = mock->max_chunk;
    }

    memcpy(buffer, mock->data + mock->offset, bytes_to_copy);
    mock->offset += bytes_to_copy;
    return (int) bytes_to_copy;
}

static esp_transport_handle_t create_mock_transport(mock_transport_data_t * mock)
{
    esp_transport_handle_t transport = esp_transport_init();
    if (transport == NULL) {
        return NULL;
    }

    if (esp_transport_set_context_data(transport, mock) != ESP_OK ||
        esp_transport_set_func(transport, NULL, mock_transport_read, NULL, NULL, NULL, NULL, NULL) != ESP_OK) {
        esp_transport_destroy(transport);
        return NULL;
    }

    return transport;
}

TEST_CASE("SV1 receive buffer handles fragmented and batched lines", "[stratum][receive]")
{
    const char * input = "first\nsecond line\nthird\n";
    mock_transport_data_t mock = {
        .data = input,
        .length = strlen(input),
        .max_chunk = 4,
    };
    esp_transport_handle_t transport = create_mock_transport(&mock);
    TEST_ASSERT_NOT_NULL(transport);
    TEST_ASSERT_TRUE(STRATUM_V1_initialize_buffer());

    char * line = STRATUM_V1_receive_jsonrpc_line(transport);
    TEST_ASSERT_EQUAL_STRING("first", line);
    free(line);

    line = STRATUM_V1_receive_jsonrpc_line(transport);
    TEST_ASSERT_EQUAL_STRING("second line", line);
    free(line);

    line = STRATUM_V1_receive_jsonrpc_line(transport);
    TEST_ASSERT_EQUAL_STRING("third", line);
    free(line);

    STRATUM_V1_cleanup_buffer();
    esp_transport_destroy(transport);
}

TEST_CASE("SV1 receive buffer discards a previous session tail", "[stratum][receive]")
{
    const char *first_input = "first\nstale\n";
    mock_transport_data_t first_mock = {
        .data = first_input,
        .length = strlen(first_input),
    };
    esp_transport_handle_t first_transport = create_mock_transport(&first_mock);
    TEST_ASSERT_NOT_NULL(first_transport);
    TEST_ASSERT_TRUE(STRATUM_V1_initialize_buffer());

    char *line = STRATUM_V1_receive_jsonrpc_line(first_transport);
    TEST_ASSERT_EQUAL_STRING("first", line);
    free(line);

    STRATUM_V1_reset_buffer();

    const char *second_input = "fresh\n";
    mock_transport_data_t second_mock = {
        .data = second_input,
        .length = strlen(second_input),
    };
    esp_transport_handle_t second_transport = create_mock_transport(&second_mock);
    TEST_ASSERT_NOT_NULL(second_transport);

    line = STRATUM_V1_receive_jsonrpc_line(second_transport);
    TEST_ASSERT_EQUAL_STRING("fresh", line);
    free(line);

    STRATUM_V1_cleanup_buffer();
    esp_transport_destroy(first_transport);
    esp_transport_destroy(second_transport);
}

TEST_CASE("SV1 receive buffer accepts the maximum line size", "[stratum][receive]")
{
    char * input = malloc(STRATUM_V1_MAX_JSON_LINE_SIZE + 2);
    TEST_ASSERT_NOT_NULL(input);
    memset(input, 'a', STRATUM_V1_MAX_JSON_LINE_SIZE);
    input[STRATUM_V1_MAX_JSON_LINE_SIZE] = '\n';
    input[STRATUM_V1_MAX_JSON_LINE_SIZE + 1] = '\0';

    mock_transport_data_t mock = {
        .data = input,
        .length = STRATUM_V1_MAX_JSON_LINE_SIZE + 1,
        .max_chunk = 251,
    };
    esp_transport_handle_t transport = create_mock_transport(&mock);
    TEST_ASSERT_NOT_NULL(transport);
    TEST_ASSERT_TRUE(STRATUM_V1_initialize_buffer());

    char * line = STRATUM_V1_receive_jsonrpc_line(transport);
    TEST_ASSERT_NOT_NULL(line);
    TEST_ASSERT_EQUAL_UINT32(STRATUM_V1_MAX_JSON_LINE_SIZE, strlen(line));

    free(line);
    free(input);
    STRATUM_V1_cleanup_buffer();
    esp_transport_destroy(transport);
}

TEST_CASE("SV1 receive buffer rejects oversized lines and recovers", "[stratum][receive]")
{
    char * oversized = malloc(STRATUM_V1_MAX_JSON_LINE_SIZE + 3);
    TEST_ASSERT_NOT_NULL(oversized);
    memset(oversized, 'a', STRATUM_V1_MAX_JSON_LINE_SIZE + 1);
    oversized[STRATUM_V1_MAX_JSON_LINE_SIZE + 1] = '\n';
    oversized[STRATUM_V1_MAX_JSON_LINE_SIZE + 2] = '\0';

    mock_transport_data_t oversized_mock = {
        .data = oversized,
        .length = STRATUM_V1_MAX_JSON_LINE_SIZE + 2,
        .max_chunk = 257,
    };
    esp_transport_handle_t oversized_transport = create_mock_transport(&oversized_mock);
    TEST_ASSERT_NOT_NULL(oversized_transport);
    TEST_ASSERT_TRUE(STRATUM_V1_initialize_buffer());

    TEST_ASSERT_NULL(STRATUM_V1_receive_jsonrpc_line(oversized_transport));
    esp_transport_destroy(oversized_transport);
    free(oversized);

    const char * valid = "valid\n";
    mock_transport_data_t valid_mock = {
        .data = valid,
        .length = strlen(valid),
    };
    esp_transport_handle_t valid_transport = create_mock_transport(&valid_mock);
    TEST_ASSERT_NOT_NULL(valid_transport);

    char * line = STRATUM_V1_receive_jsonrpc_line(valid_transport);
    TEST_ASSERT_EQUAL_STRING("valid", line);

    free(line);
    STRATUM_V1_cleanup_buffer();
    esp_transport_destroy(valid_transport);
}
