#include "unity.h"
#include "sv1_protocol.h"

#include <limits.h>
#include <string.h>

static void assert_encoded_message(const char *expected, int length, const char *actual)
{
    TEST_ASSERT_EQUAL_INT((int)strlen(expected), length);
    TEST_ASSERT_EQUAL_STRING(expected, actual);
}

static void assert_encode_did_not_fit(int length, const char *actual)
{
    TEST_ASSERT_EQUAL_INT(-1, length);
    TEST_ASSERT_EQUAL_STRING("", actual);
}

#define ASSERT_EXACT_CAPACITY(encode_call)                     \
    do {                                                       \
        char message[512];                                     \
        size_t capacity = sizeof(message);                     \
        int encoded_length = (encode_call);                    \
        TEST_ASSERT_TRUE(encoded_length > 0);                  \
        capacity = (size_t)encoded_length + 1;                 \
        TEST_ASSERT_EQUAL_INT(encoded_length, (encode_call));  \
        capacity = (size_t)encoded_length;                     \
        assert_encode_did_not_fit((encode_call), message);     \
    } while (0)

TEST_CASE("Encode SV1 setup requests", "[sv1 protocol]")
{
    char message[256];

    int length = STRATUM_V1_encode_configure_version_rolling(message, sizeof(message), 1);
    assert_encoded_message(
        "{\"id\":1,\"method\":\"mining.configure\",\"params\":[[\"version-rolling\"],{\"version-rolling.mask\":\"ffffffff\"}]}\n",
        length, message);

    length = STRATUM_V1_encode_subscribe(message, sizeof(message), 2, "BM1370", "v2.6.0");
    assert_encoded_message(
        "{\"id\":2,\"method\":\"mining.subscribe\",\"params\":[\"bitaxe/BM1370/v2.6.0\"]}\n",
        length, message);

    length = STRATUM_V1_encode_authorize(message, sizeof(message), 3,
                                         "bc1qexample.worker", "password");
    assert_encoded_message(
        "{\"id\":3,\"method\":\"mining.authorize\",\"params\":[\"bc1qexample.worker\",\"password\"]}\n",
        length, message);

    length = STRATUM_V1_encode_suggest_difficulty(message, sizeof(message), 4, UINT32_MAX);
    assert_encoded_message(
        "{\"id\":4,\"method\":\"mining.suggest_difficulty\",\"params\":[4294967295]}\n",
        length, message);

    length = STRATUM_V1_encode_extranonce_subscribe(message, sizeof(message), 5);
    assert_encoded_message(
        "{\"id\":5,\"method\":\"mining.extranonce.subscribe\",\"params\":[]}\n",
        length, message);
}

TEST_CASE("Encode SV1 client responses", "[sv1 protocol]")
{
    char message[128];

    int length = STRATUM_V1_encode_pong(message, sizeof(message), 17);
    assert_encoded_message("{\"id\":17,\"method\":\"pong\",\"params\":[]}\n", length, message);

    length = STRATUM_V1_encode_version_response(message, sizeof(message), 18, "v2.6.0");
    assert_encoded_message("{\"id\":18,\"result\":\"v2.6.0\",\"error\":null}\n", length, message);
}

TEST_CASE("Encode SV1 share submission", "[sv1 protocol]")
{
    char message[256];

    int length = STRATUM_V1_encode_submit_share(
        message, sizeof(message), 42, "bc1qexample.worker", "job-1", "00000002",
        0x01234567, 0x89abcdef, 0x00002000);

    assert_encoded_message(
        "{\"id\":42,\"method\":\"mining.submit\",\"params\":[\"bc1qexample.worker\",\"job-1\",\"00000002\",\"01234567\",\"89abcdef\",\"00002000\"]}\n",
        length, message);
}

TEST_CASE("Reject invalid SV1 encoder output buffers", "[sv1 protocol]")
{
    char message = 'x';

    TEST_ASSERT_EQUAL_INT(-1, STRATUM_V1_encode_subscribe(NULL, 1, 1, "BM1370", "v2.6.0"));
    TEST_ASSERT_EQUAL_INT(-1, STRATUM_V1_encode_subscribe(&message, 0, 1, "BM1370", "v2.6.0"));
    TEST_ASSERT_EQUAL_CHAR('x', message);

    TEST_ASSERT_EQUAL_INT(-1, STRATUM_V1_encode_suggest_difficulty(NULL, 1, 1, 1));
    TEST_ASSERT_EQUAL_INT(-1, STRATUM_V1_encode_suggest_difficulty(&message, 0, 1, 1));

    TEST_ASSERT_EQUAL_INT(-1, STRATUM_V1_encode_extranonce_subscribe(NULL, 1, 1));
    TEST_ASSERT_EQUAL_INT(-1, STRATUM_V1_encode_extranonce_subscribe(&message, 0, 1));

    TEST_ASSERT_EQUAL_INT(-1, STRATUM_V1_encode_authorize(NULL, 1, 1, "worker", "password"));
    TEST_ASSERT_EQUAL_INT(-1, STRATUM_V1_encode_authorize(&message, 0, 1, "worker", "password"));

    TEST_ASSERT_EQUAL_INT(-1, STRATUM_V1_encode_pong(NULL, 1, 1));
    TEST_ASSERT_EQUAL_INT(-1, STRATUM_V1_encode_pong(&message, 0, 1));

    TEST_ASSERT_EQUAL_INT(-1, STRATUM_V1_encode_version_response(NULL, 1, 1, "v2.6.0"));
    TEST_ASSERT_EQUAL_INT(-1, STRATUM_V1_encode_version_response(&message, 0, 1, "v2.6.0"));

    TEST_ASSERT_EQUAL_INT(
        -1, STRATUM_V1_encode_submit_share(NULL, 1, 1, "worker", "job", "00", 0, 0, 0));
    TEST_ASSERT_EQUAL_INT(
        -1, STRATUM_V1_encode_submit_share(&message, 0, 1, "worker", "job", "00", 0, 0, 0));

    TEST_ASSERT_EQUAL_INT(-1, STRATUM_V1_encode_configure_version_rolling(NULL, 1, 1));
    TEST_ASSERT_EQUAL_INT(-1, STRATUM_V1_encode_configure_version_rolling(&message, 0, 1));
}

TEST_CASE("Reject missing SV1 encoder fields", "[sv1 protocol]")
{
    char message[128];

    strcpy(message, "stale");
    TEST_ASSERT_EQUAL_INT(-1, STRATUM_V1_encode_subscribe(message, sizeof(message), 1, NULL, "v2.6.0"));
    TEST_ASSERT_EQUAL_STRING("", message);
    strcpy(message, "stale");
    TEST_ASSERT_EQUAL_INT(-1, STRATUM_V1_encode_subscribe(message, sizeof(message), 1, "BM1370", NULL));
    TEST_ASSERT_EQUAL_STRING("", message);

    strcpy(message, "stale");
    TEST_ASSERT_EQUAL_INT(-1, STRATUM_V1_encode_authorize(message, sizeof(message), 1, NULL, "password"));
    TEST_ASSERT_EQUAL_STRING("", message);
    strcpy(message, "stale");
    TEST_ASSERT_EQUAL_INT(-1, STRATUM_V1_encode_authorize(message, sizeof(message), 1, "worker", NULL));
    TEST_ASSERT_EQUAL_STRING("", message);

    strcpy(message, "stale");
    TEST_ASSERT_EQUAL_INT(-1, STRATUM_V1_encode_version_response(message, sizeof(message), 1, NULL));
    TEST_ASSERT_EQUAL_STRING("", message);

    strcpy(message, "stale");
    TEST_ASSERT_EQUAL_INT(
        -1, STRATUM_V1_encode_submit_share(message, sizeof(message), 1, NULL, "job", "00", 0, 0, 0));
    TEST_ASSERT_EQUAL_STRING("", message);
    strcpy(message, "stale");
    TEST_ASSERT_EQUAL_INT(
        -1, STRATUM_V1_encode_submit_share(message, sizeof(message), 1, "worker", NULL, "00", 0, 0, 0));
    TEST_ASSERT_EQUAL_STRING("", message);
    strcpy(message, "stale");
    TEST_ASSERT_EQUAL_INT(
        -1, STRATUM_V1_encode_submit_share(message, sizeof(message), 1, "worker", "job", NULL, 0, 0, 0));
    TEST_ASSERT_EQUAL_STRING("", message);
}

TEST_CASE("Reject every truncated SV1 message", "[sv1 protocol]")
{
    char message[1];

    assert_encode_did_not_fit(
        STRATUM_V1_encode_subscribe(message, sizeof(message), 1, "BM1370", "v2.6.0"), message);
    assert_encode_did_not_fit(
        STRATUM_V1_encode_suggest_difficulty(message, sizeof(message), 1, 1), message);
    assert_encode_did_not_fit(
        STRATUM_V1_encode_extranonce_subscribe(message, sizeof(message), 1), message);
    assert_encode_did_not_fit(
        STRATUM_V1_encode_authorize(message, sizeof(message), 1, "worker", "password"), message);
    assert_encode_did_not_fit(STRATUM_V1_encode_pong(message, sizeof(message), 1), message);
    assert_encode_did_not_fit(
        STRATUM_V1_encode_version_response(message, sizeof(message), 1, "v2.6.0"), message);
    assert_encode_did_not_fit(
        STRATUM_V1_encode_submit_share(
            message, sizeof(message), 1, "worker", "job", "00", 0, 0, 0),
        message);
    assert_encode_did_not_fit(
        STRATUM_V1_encode_configure_version_rolling(message, sizeof(message), 1), message);
}

TEST_CASE("Escape every SV1 JSON string field", "[sv1 protocol]")
{
    char message[512];

    int length = STRATUM_V1_encode_subscribe(
        message, sizeof(message), 1, "BM\"1370", "v2\\next");
    assert_encoded_message(
        "{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[\"bitaxe/BM\\\"1370/v2\\\\next\"]}\n",
        length, message);

    length = STRATUM_V1_encode_authorize(
        message, sizeof(message), 2, "worker\"\\\n", "\b\f\r\t\001");
    assert_encoded_message(
        "{\"id\":2,\"method\":\"mining.authorize\",\"params\":[\"worker\\\"\\\\\\n\",\"\\b\\f\\r\\t\\u0001\"]}\n",
        length, message);

    length = STRATUM_V1_encode_version_response(
        message, sizeof(message), 3, "v2\"\\\n");
    assert_encoded_message(
        "{\"id\":3,\"result\":\"v2\\\"\\\\\\n\",\"error\":null}\n",
        length, message);

    length = STRATUM_V1_encode_submit_share(
        message, sizeof(message), 4, "worker\"", "job\\", "00\n",
        0, UINT32_MAX, 0x00002000);
    assert_encoded_message(
        "{\"id\":4,\"method\":\"mining.submit\",\"params\":[\"worker\\\"\",\"job\\\\\",\"00\\n\",\"00000000\",\"ffffffff\",\"00002000\"]}\n",
        length, message);
}

TEST_CASE("Encode SV1 messages at exact buffer boundaries", "[sv1 protocol]")
{
    ASSERT_EXACT_CAPACITY(STRATUM_V1_encode_subscribe(
        message, capacity, INT_MAX, "BM1370", "v2.6.0"));
    ASSERT_EXACT_CAPACITY(STRATUM_V1_encode_suggest_difficulty(
        message, capacity, INT_MIN, UINT32_MAX));
    ASSERT_EXACT_CAPACITY(STRATUM_V1_encode_extranonce_subscribe(
        message, capacity, INT_MAX));
    ASSERT_EXACT_CAPACITY(STRATUM_V1_encode_authorize(
        message, capacity, INT_MIN, "worker", ""));
    ASSERT_EXACT_CAPACITY(STRATUM_V1_encode_pong(
        message, capacity, INT_MAX));
    ASSERT_EXACT_CAPACITY(STRATUM_V1_encode_version_response(
        message, capacity, INT_MIN, "unknown"));
    ASSERT_EXACT_CAPACITY(STRATUM_V1_encode_submit_share(
        message, capacity, INT_MAX, "worker", "job", "", 0, UINT32_MAX, UINT32_MAX));
    ASSERT_EXACT_CAPACITY(STRATUM_V1_encode_configure_version_rolling(
        message, capacity, INT_MIN));
}
