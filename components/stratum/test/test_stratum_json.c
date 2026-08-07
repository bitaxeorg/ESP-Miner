#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "stratum_api.h"

typedef struct {
    const char *data;
    size_t length;
    size_t offset;
    size_t max_chunk;
} mock_transport_data_t;

static int mock_transport_read(esp_transport_handle_t transport, char *buffer,
                               int len, int timeout_ms)
{
    mock_transport_data_t *mock = esp_transport_get_context_data(transport);
    if (mock == NULL || mock->offset >= mock->length) {
        return ERR_TCP_TRANSPORT_CONNECTION_CLOSED_BY_FIN;
    }

    size_t bytes_remaining = mock->length - mock->offset;
    size_t bytes_to_copy = bytes_remaining;
    if (bytes_to_copy > (size_t)len) {
        bytes_to_copy = (size_t)len;
    }
    if (bytes_to_copy > mock->max_chunk) {
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

TEST_CASE("Receive fragmented JSON-RPC line", "[stratum][security]")
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
    TEST_ASSERT_EQUAL_STRING("{\"id\":1,\"result\":true,\"error\":null}",
                             line);

    free(line);
    esp_transport_destroy(transport);
}

TEST_CASE("Receive preserves consecutive JSON-RPC lines", "[stratum][security]")
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

TEST_CASE("Receive rejects oversized JSON-RPC line", "[stratum][security]")
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

TEST_CASE("Receive accepts maximum line and preserves the next line", "[stratum][security]")
{
    const char *next_line = "{}\n";
    size_t data_length = STRATUM_V1_MAX_JSON_LINE_SIZE + 1U +
                         strlen(next_line);
    char *data = malloc(data_length);
    TEST_ASSERT_NOT_NULL(data);
    memset(data, 'a', STRATUM_V1_MAX_JSON_LINE_SIZE);
    data[STRATUM_V1_MAX_JSON_LINE_SIZE] = '\n';
    memcpy(data + STRATUM_V1_MAX_JSON_LINE_SIZE + 1U, next_line,
           strlen(next_line));

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
    TEST_ASSERT_EQUAL_size_t(STRATUM_V1_MAX_JSON_LINE_SIZE,
                             strlen(maximum_line));
    TEST_ASSERT_EQUAL_STRING("{}", second_line);

    free(maximum_line);
    free(second_line);
    esp_transport_destroy(transport);
    free(data);
}

TEST_CASE("Receive rejects embedded NUL", "[stratum][security]")
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

TEST_CASE("Parse stratum method", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_message = {};

    const char *json_string_standard = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                                       "[\"1b4c3d9041\","
                                       "\"ef4b9a48c7986466de4adc002f7337a6e121bc43000376ea0000000000000000\","
                                       "\"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b03a5020cfabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000\","
                                       "\"41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000\","
                                       "[\"ae23055e00f0f697cc3640124812d96d4fe8bdfa03484c1c638ce5a1c0e9aa81\",\"980fb87cb61021dd7afd314fcb0dabd096f3d56a7377f6f320684652e7410a21\",\"a52e9868343c55ce405be8971ff340f562ae9ab6353f07140d01666180e19b52\",\"7435bdfa004e603953b2ed39f118803934d9cf17b06d979ceb682f2251bafac2\",\"2a91f061a22d27cb8f44eea79938fb241ebeb359891aa907f05ffde7ed44e52e\",\"302401f80eb5e958155135e25200bb8ea181ad2d05e804a531c7314d86403cdc\",\"318ecb6161eb9b4cfd802bd730e2d36c167ddf102e70aa7b4158e2870dd47392\",\"1114332a9858e0cf84b2425bb1e59eaabf91dd102d114aa443d57fc1b3beb0c9\",\"f43f38095c810613ed795a44d9fab02ff25269706f454885db9be05cdf9c06e1\",\"3e2fc26b27fddc39668b59099cd9635761bb72ed92404204e12bdff08b16fb75\",\"463c19427286342120039a83218fa87ce45448e246895abac11fff0036076758\",\"03d287f655813e540ddb9c4e7aeb922478662b0f5d8e9d0cbd564b20146bab76\"],"
                                       "\"20000004\",\"1705c739\",\"64495522\",false]}";

    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string_standard));
    TEST_ASSERT_EQUAL(MINING_NOTIFY, stratum_api_v1_message.method);
    TEST_ASSERT_FALSE(stratum_api_v1_message.mining_notification->clean_jobs);
}

TEST_CASE("Parse stratum mining.notify abandon work", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_message = {};

    const char *json_string_abandon_work_false = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                                                 "[\"1b4c3d9041\","
                                                 "\"ef4b9a48c7986466de4adc002f7337a6e121bc43000376ea0000000000000000\","
                                                 "\"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b03a5020cfabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000\","
                                                 "\"41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000\","
                                                 "[\"ae23055e00f0f697cc3640124812d96d4fe8bdfa03484c1c638ce5a1c0e9aa81\",\"980fb87cb61021dd7afd314fcb0dabd096f3d56a7377f6f320684652e7410a21\",\"a52e9868343c55ce405be8971ff340f562ae9ab6353f07140d01666180e19b52\",\"7435bdfa004e603953b2ed39f118803934d9cf17b06d979ceb682f2251bafac2\",\"2a91f061a22d27cb8f44eea79938fb241ebeb359891aa907f05ffde7ed44e52e\",\"302401f80eb5e958155135e25200bb8ea181ad2d05e804a531c7314d86403cdc\",\"318ecb6161eb9b4cfd802bd730e2d36c167ddf102e70aa7b4158e2870dd47392\",\"1114332a9858e0cf84b2425bb1e59eaabf91dd102d114aa443d57fc1b3beb0c9\",\"f43f38095c810613ed795a44d9fab02ff25269706f454885db9be05cdf9c06e1\",\"3e2fc26b27fddc39668b59099cd9635761bb72ed92404204e12bdff08b16fb75\",\"463c19427286342120039a83218fa87ce45448e246895abac11fff0036076758\",\"03d287f655813e540ddb9c4e7aeb922478662b0f5d8e9d0cbd564b20146bab76\"],"
                                                 "\"20000004\",\"1705c739\",\"64495522\",false]}";

    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string_abandon_work_false));
    TEST_ASSERT_EQUAL(MINING_NOTIFY, stratum_api_v1_message.method);
    TEST_ASSERT_FALSE(stratum_api_v1_message.mining_notification->clean_jobs);

    const char *json_string_abandon_work = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                                           "[\"1b4c3d9041\","
                                           "\"ef4b9a48c7986466de4adc002f7337a6e121bc43000376ea0000000000000000\","
                                           "\"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b03a5020cfabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000\","
                                           "\"41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000\","
                                           "[\"ae23055e00f0f697cc3640124812d96d4fe8bdfa03484c1c638ce5a1c0e9aa81\",\"980fb87cb61021dd7afd314fcb0dabd096f3d56a7377f6f320684652e7410a21\",\"a52e9868343c55ce405be8971ff340f562ae9ab6353f07140d01666180e19b52\",\"7435bdfa004e603953b2ed39f118803934d9cf17b06d979ceb682f2251bafac2\",\"2a91f061a22d27cb8f44eea79938fb241ebeb359891aa907f05ffde7ed44e52e\",\"302401f80eb5e958155135e25200bb8ea181ad2d05e804a531c7314d86403cdc\",\"318ecb6161eb9b4cfd802bd730e2d36c167ddf102e70aa7b4158e2870dd47392\",\"1114332a9858e0cf84b2425bb1e59eaabf91dd102d114aa443d57fc1b3beb0c9\",\"f43f38095c810613ed795a44d9fab02ff25269706f454885db9be05cdf9c06e1\",\"3e2fc26b27fddc39668b59099cd9635761bb72ed92404204e12bdff08b16fb75\",\"463c19427286342120039a83218fa87ce45448e246895abac11fff0036076758\",\"03d287f655813e540ddb9c4e7aeb922478662b0f5d8e9d0cbd564b20146bab76\"],"
                                           "\"20000004\",\"1705c739\",\"64495522\",true]}";

    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string_abandon_work));
    TEST_ASSERT_EQUAL(MINING_NOTIFY, stratum_api_v1_message.method);
    TEST_ASSERT_TRUE(stratum_api_v1_message.mining_notification->clean_jobs);

    const char *json_string_abandon_work_length_9 = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                                                    "[\"1b4c3d9041\","
                                                    "\"ef4b9a48c7986466de4adc002f7337a6e121bc43000376ea0000000000000000\","
                                                    "\"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b03a5020cfabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000\","
                                                    "\"41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000\","
                                                    "[\"ae23055e00f0f697cc3640124812d96d4fe8bdfa03484c1c638ce5a1c0e9aa81\",\"980fb87cb61021dd7afd314fcb0dabd096f3d56a7377f6f320684652e7410a21\",\"a52e9868343c55ce405be8971ff340f562ae9ab6353f07140d01666180e19b52\",\"7435bdfa004e603953b2ed39f118803934d9cf17b06d979ceb682f2251bafac2\",\"2a91f061a22d27cb8f44eea79938fb241ebeb359891aa907f05ffde7ed44e52e\",\"302401f80eb5e958155135e25200bb8ea181ad2d05e804a531c7314d86403cdc\",\"318ecb6161eb9b4cfd802bd730e2d36c167ddf102e70aa7b4158e2870dd47392\",\"1114332a9858e0cf84b2425bb1e59eaabf91dd102d114aa443d57fc1b3beb0c9\",\"f43f38095c810613ed795a44d9fab02ff25269706f454885db9be05cdf9c06e1\",\"3e2fc26b27fddc39668b59099cd9635761bb72ed92404204e12bdff08b16fb75\",\"463c19427286342120039a83218fa87ce45448e246895abac11fff0036076758\",\"03d287f655813e540ddb9c4e7aeb922478662b0f5d8e9d0cbd564b20146bab76\"],"
                                                    "\"20000004\",\"1705c739\",\"64495522\",\"64495522\",true]}";

    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string_abandon_work_length_9));
    TEST_ASSERT_EQUAL(MINING_NOTIFY, stratum_api_v1_message.method);
    TEST_ASSERT_TRUE(stratum_api_v1_message.mining_notification->clean_jobs);
}

TEST_CASE("Parse stratum set_difficulty params", "[mining.set_difficulty]")
{
    const char *json_string = "{\"id\":null,\"method\":\"mining.set_difficulty\",\"params\":[1638]}";
    StratumApiV1Message stratum_api_v1_message = {};
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(MINING_SET_DIFFICULTY, stratum_api_v1_message.method);
    TEST_ASSERT_EQUAL_DOUBLE(1638.0, stratum_api_v1_message.new_difficulty);
}

TEST_CASE("Parse stratum set_difficulty params with fractional", "[mining.set_difficulty]")
{
    const char *json_string = "{\"id\":null,\"method\":\"mining.set_difficulty\",\"params\":[100.5]}";
    StratumApiV1Message stratum_api_v1_message = {};
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(MINING_SET_DIFFICULTY, stratum_api_v1_message.method);
    TEST_ASSERT_EQUAL_DOUBLE(100.5, stratum_api_v1_message.new_difficulty);
}

TEST_CASE("Parse stratum notify params", "[mining.notify]")
{
    StratumApiV1Message stratum_api_v1_message = {};
    const char *json_string = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                              "[\"1d2e0c4d3d\","
                              "\"ef4b9a48c7986466de4adc002f7337a6e121bc43000376ea0000000000000000\","
                              "\"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b03a5020cfabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000\","
                              "\"41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000\","
                              "[\"ae23055e00f0f697cc3640124812d96d4fe8bdfa03484c1c638ce5a1c0e9aa81\",\"980fb87cb61021dd7afd314fcb0dabd096f3d56a7377f6f320684652e7410a21\",\"a52e9868343c55ce405be8971ff340f562ae9ab6353f07140d01666180e19b52\",\"7435bdfa004e603953b2ed39f118803934d9cf17b06d979ceb682f2251bafac2\",\"2a91f061a22d27cb8f44eea79938fb241ebeb359891aa907f05ffde7ed44e52e\",\"302401f80eb5e958155135e25200bb8ea181ad2d05e804a531c7314d86403cdc\",\"318ecb6161eb9b4cfd802bd730e2d36c167ddf102e70aa7b4158e2870dd47392\",\"1114332a9858e0cf84b2425bb1e59eaabf91dd102d114aa443d57fc1b3beb0c9\",\"f43f38095c810613ed795a44d9fab02ff25269706f454885db9be05cdf9c06e1\",\"3e2fc26b27fddc39668b59099cd9635761bb72ed92404204e12bdff08b16fb75\",\"463c19427286342120039a83218fa87ce45448e246895abac11fff0036076758\",\"03d287f655813e540ddb9c4e7aeb922478662b0f5d8e9d0cbd564b20146bab76\"],"
                              "\"20000004\",\"1705c739\",\"64495522\",false]}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL_STRING("1d2e0c4d3d", stratum_api_v1_message.mining_notification->job_id);
    TEST_ASSERT_EQUAL_STRING("ef4b9a48c7986466de4adc002f7337a6e121bc43000376ea0000000000000000", stratum_api_v1_message.mining_notification->prev_block_hash);
    TEST_ASSERT_EQUAL_STRING("01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b03a5020cfabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000", stratum_api_v1_message.mining_notification->coinbase_1);
    TEST_ASSERT_EQUAL_STRING("41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000", stratum_api_v1_message.mining_notification->coinbase_2);
    TEST_ASSERT_EQUAL_UINT32(0x20000004, stratum_api_v1_message.mining_notification->version);
    TEST_ASSERT_EQUAL_UINT32(0x1705c739, stratum_api_v1_message.mining_notification->target);
    TEST_ASSERT_EQUAL_UINT32(0x64495522, stratum_api_v1_message.mining_notification->ntime);
}

TEST_CASE("Test mining.subcribe result parsing", "[mining.subscribe]")
{
    StratumApiV1Message stratum_api_v1_message = {};
    const char * json_string = "{\"result\":[[[\"mining.notify\",\"695482c0\"]],\"4de05269\",8],\"id\":2,\"error\":null}";

    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL_STRING("4de05269", stratum_api_v1_message.extranonce_str);
    TEST_ASSERT_EQUAL_INT(8, stratum_api_v1_message.extranonce_2_len);
}

TEST_CASE("Parse stratum mining.subscribe result malformed", "[mining.subscribe]")
{
    // Only 2 array items — extranonce2_len is missing
    StratumApiV1Message stratum_api_v1_message = {};
    const char *json_string = "{\"result\":[[[\"mining.notify\",\"abc\"]],\"4de05269\"],\"id\":2,\"error\":null}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
}

TEST_CASE("Parse stratum mining.set_version_mask params", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_message = {};
    const char *json_string = "{\"id\":1,\"method\":\"mining.set_version_mask\",\"params\":[\"1fffe000\"]}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(1, stratum_api_v1_message.message_id);
    TEST_ASSERT_EQUAL(MINING_SET_VERSION_MASK, stratum_api_v1_message.method);
    TEST_ASSERT_EQUAL_HEX32(0x1fffe000, stratum_api_v1_message.version_mask);
}

TEST_CASE("Parse stratum result success", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_setup_message = {};
    const char* resp1 = "{\"id\":4,\"error\":null,\"result\":true}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_setup_message, resp1));
    TEST_ASSERT_EQUAL(4, stratum_api_v1_setup_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_setup_message.method);
    TEST_ASSERT_TRUE(stratum_api_v1_setup_message.response_success);

    StratumApiV1Message stratum_api_v1_message = {};
    const char* json_string = "{\"id\":5,\"error\":null,\"result\":true}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(5, stratum_api_v1_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_message.method);
    TEST_ASSERT_TRUE(stratum_api_v1_message.response_success);
}

TEST_CASE("Parse stratum result success with large id", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_message = {};
    const char *json_string = "{\"id\":32769,\"error\":null,\"result\":true}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(32769, stratum_api_v1_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_message.method);
    TEST_ASSERT_TRUE(stratum_api_v1_message.response_success);
}

TEST_CASE("Parse stratum result success with larger id", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_message = {};
    const char *json_string = "{\"id\":65536,\"error\":null,\"result\":true}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(65536, stratum_api_v1_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_message.method);
    TEST_ASSERT_TRUE(stratum_api_v1_message.response_success);
}

TEST_CASE("Parse stratum result error", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_setup_message = {};
    const char* resp1 = "{\"id\":4,\"result\":null,\"error\":[21,\"Job not found\",\"\"]}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_setup_message, resp1));
    TEST_ASSERT_EQUAL(4, stratum_api_v1_setup_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_setup_message.method);
    TEST_ASSERT_FALSE(stratum_api_v1_setup_message.response_success);
    TEST_ASSERT_EQUAL_STRING("Job not found", stratum_api_v1_setup_message.error_str);

    StratumApiV1Message stratum_api_v1_message = {};
    const char* json_string = "{\"id\":5,\"result\":null,\"error\":[21,\"Job not found\",\"\"]}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(5, stratum_api_v1_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_message.method);
    TEST_ASSERT_FALSE(stratum_api_v1_message.response_success);
    TEST_ASSERT_EQUAL_STRING("Job not found", stratum_api_v1_message.error_str);
}

TEST_CASE("Parse stratum result alternative error", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_message = {};
    const char *json_string = "{\"reject-reason\":\"Above target 2\",\"result\":false,\"error\":null,\"id\":8}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(8, stratum_api_v1_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_message.method);
    TEST_ASSERT_FALSE(stratum_api_v1_message.response_success);
    TEST_ASSERT_EQUAL_STRING("Above target 2", stratum_api_v1_message.error_str);
}

TEST_CASE("Parse stratum result with error string (Stale)", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_message = {};
    const char *json_string = "{\"result\":false,\"error\":\"Stale\",\"id\":618}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(618, stratum_api_v1_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_message.method);
    TEST_ASSERT_FALSE(stratum_api_v1_message.response_success);
    TEST_ASSERT_EQUAL_STRING("Stale", stratum_api_v1_message.error_str);
}

TEST_CASE("Parse stratum result with null result and error string", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_message = {};
    const char *json_string = "{\"result\":null,\"error\":\"Stale\",\"id\":618}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(618, stratum_api_v1_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_message.method);
    TEST_ASSERT_FALSE(stratum_api_v1_message.response_success);
    TEST_ASSERT_EQUAL_STRING("Stale", stratum_api_v1_message.error_str);
}

TEST_CASE("Parse stratum error array format", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_message = {};
    const char *json_string = "{\"id\":50,\"result\":null,\"error\":[21,\"Job not found\",\"\"]}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(50, stratum_api_v1_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_message.method);
    TEST_ASSERT_FALSE(stratum_api_v1_message.response_success);
    TEST_ASSERT_EQUAL_STRING("Job not found", stratum_api_v1_message.error_str);
}

TEST_CASE("Parse stratum error jsonrpc object with code", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_message = {};
    const char *json_string = "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":22,\"message\":\"duplicate share\",\"data\":null},\"id\":42}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(42, stratum_api_v1_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_message.method);
    TEST_ASSERT_FALSE(stratum_api_v1_message.response_success);
    TEST_ASSERT_EQUAL_STRING("duplicate share", stratum_api_v1_message.error_str);
}

TEST_CASE("Parse stratum invalid json or malformed parameters", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_message = {};
    const char *json_string = "{\"id\":null,\"method\":\"mining.notify\",\"params\":[]}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));

    StratumApiV1Message stratum_api_v1_message2 = {};
    const char *json_string2 = "invalid json";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&stratum_api_v1_message2, json_string2));
}

TEST_CASE("Parse stratum mining.set_extranonce params", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_message = {};
    const char *json_string = "{\"id\":1,\"method\":\"mining.set_extranonce\",\"params\":[\"deadbeef\",8]}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(MINING_SET_EXTRANONCE, stratum_api_v1_message.method);
    TEST_ASSERT_EQUAL_STRING("deadbeef", stratum_api_v1_message.extranonce_str);
    TEST_ASSERT_EQUAL_INT(8, stratum_api_v1_message.extranonce_2_len);
}

TEST_CASE("Parse stratum mining.set_extranonce invalid params", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_message = {};
    const char *json_string = "{\"id\":1,\"method\":\"mining.set_extranonce\",\"params\":[]}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
}

TEST_CASE("Parse stratum client.show_message", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_message = {};
    const char *json_string = "{\"id\":null,\"method\":\"client.show_message\",\"params\":[\"Welcome to the pool!\"]}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(CLIENT_SHOW_MESSAGE, stratum_api_v1_message.method);
    TEST_ASSERT_EQUAL_STRING("Welcome to the pool!", stratum_api_v1_message.show_message);
}

TEST_CASE("Parse stratum client.show_message invalid params", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_message = {};
    const char *json_string = "{\"id\":null,\"method\":\"client.show_message\",\"params\":[]}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
}

TEST_CASE("Parse stratum client.get_version", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_message = {};
    const char *json_string = "{\"id\":10,\"method\":\"client.get_version\",\"params\":[]}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(CLIENT_GET_VERSION, stratum_api_v1_message.method);
    TEST_ASSERT_EQUAL_STRING("unknown", stratum_api_v1_message.version_string);
}

TEST_CASE("Parse stratum client.reconnect", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_message = {};
    const char *json_string = "{\"id\":null,\"method\":\"client.reconnect\",\"params\":[]}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(CLIENT_RECONNECT, stratum_api_v1_message.method);
}

TEST_CASE("Parse stratum mining.ping", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_message = {};
    const char *json_string = "{\"id\":null,\"method\":\"mining.ping\",\"params\":[]}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(MINING_PING, stratum_api_v1_message.method);
}

TEST_CASE("Parse stratum unknown method returns false", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_message = {};
    const char *json_string = "{\"id\":null,\"method\":\"mining.hashrate\",\"params\":[]}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
}

TEST_CASE("Parse stratum configure result", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_message = {};
    const char *json_string = "{\"id\":1,\"result\":{\"version-rolling\":true,\"version-rolling.mask\":\"1fffe000\"},\"error\":null}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(STRATUM_RESULT_CONFIGURE, stratum_api_v1_message.method);
    TEST_ASSERT_TRUE(stratum_api_v1_message.response_success);
    TEST_ASSERT_EQUAL_HEX32(0x1fffe000, stratum_api_v1_message.version_mask);
}

TEST_CASE("Reject malformed mining.notify fields", "[stratum][security]")
{
    StratumApiV1Message message = {};
    const char *valid_notify =
        "{\"id\":null,\"method\":\"mining.notify\",\"params\":["
        "\"job\","
        "\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"00\",\"00\",[],\"20000000\",\"1d00ffff\",\"00000000\",true]}";
    const char *wrong_type =
        "{\"id\":null,\"method\":\"mining.notify\",\"params\":["
        "\"job\",7,\"00\",\"00\",[],\"20000000\",\"1d00ffff\",\"00000000\",true]}";
    const char *invalid_merkle =
        "{\"id\":null,\"method\":\"mining.notify\",\"params\":["
        "\"job\","
        "\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"00\",\"00\",[\"xyz\"],\"20000000\",\"1d00ffff\",\"00000000\",true]}";
    const char *short_version =
        "{\"id\":null,\"method\":\"mining.notify\",\"params\":["
        "\"job\","
        "\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"00\",\"00\",[],\"20\",\"1d00ffff\",\"00000000\",true]}";

    TEST_ASSERT_TRUE(STRATUM_V1_parse(&message, valid_notify));
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&message, wrong_type));
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&message, invalid_merkle));
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&message, short_version));
    STRATUM_V1_reset_message(&message);
}

TEST_CASE("Reject unsafe extranonce lengths", "[stratum][security]")
{
    StratumApiV1Message message = {};

    TEST_ASSERT_FALSE(STRATUM_V1_parse(
        &message,
        "{\"id\":1,\"method\":\"mining.set_extranonce\",\"params\":[\"deadbeef\",-1]}"));
    TEST_ASSERT_FALSE(STRATUM_V1_parse(
        &message,
        "{\"id\":1,\"method\":\"mining.set_extranonce\",\"params\":[\"deadbeef\",33]}"));
    TEST_ASSERT_FALSE(STRATUM_V1_parse(
        &message,
        "{\"id\":1,\"method\":\"mining.set_extranonce\",\"params\":[\"deadbeef\",1.5]}"));
    TEST_ASSERT_FALSE(STRATUM_V1_parse(
        &message,
        "{\"result\":[[],\"deadbeef\",-1],\"id\":2,\"error\":null}"));
}

TEST_CASE("Reject invalid numeric and JSON-RPC values", "[stratum][security]")
{
    StratumApiV1Message message = {};

    TEST_ASSERT_FALSE(STRATUM_V1_parse(
        &message,
        "{\"id\":null,\"method\":\"mining.set_difficulty\",\"params\":[0]}"));
    TEST_ASSERT_FALSE(STRATUM_V1_parse(
        &message,
        "{\"id\":null,\"method\":\"mining.set_version_mask\",\"params\":[\"xyz\"]}"));
    TEST_ASSERT_FALSE(STRATUM_V1_parse(
        &message,
        "{\"id\":1.5,\"result\":true,\"error\":null}"));
    TEST_ASSERT_FALSE(STRATUM_V1_parse(
        &message,
        "{\"id\":1,\"result\":true,\"error\":null} trailing"));
}
