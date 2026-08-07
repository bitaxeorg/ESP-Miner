#include <string.h>

#include "unity.h"

#include "bap_protocol.h"

TEST_CASE("BAP formatter creates a complete checksummed frame", "[bap][protocol]") {
    char message[BAP_MAX_MESSAGE_LEN];
    size_t message_len = 0;

    TEST_ASSERT_TRUE(BAP_format_message(message, sizeof(message), BAP_CMD_ACK,
                                        "status", "ok", &message_len));
    TEST_ASSERT_EQUAL_STRING("$BAP,ACK,status,ok*26\r\n", message);
    TEST_ASSERT_EQUAL(strlen(message), message_len);
}

TEST_CASE("BAP formatter accepts only complete maximum-length frames", "[bap][protocol]") {
    char message[BAP_MAX_MESSAGE_LEN];
    char value[241];
    size_t message_len = 0;

    memset(value, 'v', 239);
    value[239] = '\0';
    TEST_ASSERT_TRUE(BAP_format_message(message, sizeof(message), BAP_CMD_ACK,
                                        "p", value, &message_len));
    TEST_ASSERT_EQUAL(BAP_MAX_MESSAGE_LEN - 1, message_len);
    TEST_ASSERT_EQUAL_CHAR('\0', message[message_len]);

    value[239] = 'v';
    value[240] = '\0';
    TEST_ASSERT_FALSE(BAP_format_message(message, sizeof(message), BAP_CMD_ACK,
                                         "p", value, &message_len));
    TEST_ASSERT_EQUAL(0, message_len);
    TEST_ASSERT_EQUAL_CHAR('\0', message[0]);
}

TEST_CASE("BAP formatter rejects an oversized sentence body", "[bap][protocol]") {
    char message[BAP_MAX_MESSAGE_LEN];
    char value[247];
    size_t message_len = 1;

    memset(value, 'v', sizeof(value) - 1);
    value[sizeof(value) - 1] = '\0';

    TEST_ASSERT_FALSE(BAP_format_message(message, sizeof(message), BAP_CMD_ACK,
                                         "p", value, &message_len));
    TEST_ASSERT_EQUAL(0, message_len);
    TEST_ASSERT_EQUAL_CHAR('\0', message[0]);
}

TEST_CASE("BAP formatter rejects invalid or undersized output buffers", "[bap][protocol]") {
    char message[8] = "pending";
    size_t message_len = 1;

    TEST_ASSERT_FALSE(BAP_format_message(message, sizeof(message), BAP_CMD_ACK,
                                         "status", "ok", &message_len));
    TEST_ASSERT_EQUAL(0, message_len);
    TEST_ASSERT_EQUAL_CHAR('\0', message[0]);

    TEST_ASSERT_FALSE(BAP_format_message(message, sizeof(message), BAP_CMD_ACK,
                                         NULL, "ok", &message_len));
    TEST_ASSERT_EQUAL(0, message_len);

    TEST_ASSERT_FALSE(BAP_format_message(message, sizeof(message), BAP_CMD_ACK,
                                         "status", "ok", NULL));
}
