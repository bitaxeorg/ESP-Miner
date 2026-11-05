#include "unity.h"

#include "webhook_alert_utils.h"

#define TEST_URL_MAX_LEN 512

TEST_CASE("Webhook URLs accept valid HTTPS authorities", "[webhook_alert_utils]")
{
    TEST_ASSERT_TRUE(WEBHOOK_ALERT_UTILS_is_valid_url("https://example.com/hook", TEST_URL_MAX_LEN));
    TEST_ASSERT_TRUE(WEBHOOK_ALERT_UTILS_is_valid_url("https://discord.com:443/api/webhooks/1/token",
                                                      TEST_URL_MAX_LEN));
    TEST_ASSERT_TRUE(WEBHOOK_ALERT_UTILS_is_valid_url("https://example.com:1/hook", TEST_URL_MAX_LEN));
    TEST_ASSERT_TRUE(WEBHOOK_ALERT_UTILS_is_valid_url("https://example.com:65535/hook",
                                                      TEST_URL_MAX_LEN));
}

TEST_CASE("Webhook URLs reject authorities that could leak secrets", "[webhook_alert_utils]")
{
    TEST_ASSERT_FALSE(WEBHOOK_ALERT_UTILS_is_valid_url("http://example.com/hook", TEST_URL_MAX_LEN));
    TEST_ASSERT_FALSE(WEBHOOK_ALERT_UTILS_is_valid_url("https://user@example.com/hook", TEST_URL_MAX_LEN));
    TEST_ASSERT_FALSE(WEBHOOK_ALERT_UTILS_is_valid_url("https://example.com:/hook", TEST_URL_MAX_LEN));
    TEST_ASSERT_FALSE(WEBHOOK_ALERT_UTILS_is_valid_url("https://example.com:notaport/hook", TEST_URL_MAX_LEN));
    TEST_ASSERT_FALSE(WEBHOOK_ALERT_UTILS_is_valid_url("https://example.com:0/hook", TEST_URL_MAX_LEN));
    TEST_ASSERT_FALSE(WEBHOOK_ALERT_UTILS_is_valid_url("https://example.com:65536/hook", TEST_URL_MAX_LEN));
    TEST_ASSERT_FALSE(WEBHOOK_ALERT_UTILS_is_valid_url("https://example.com:443:444/hook", TEST_URL_MAX_LEN));
    TEST_ASSERT_FALSE(WEBHOOK_ALERT_UTILS_is_valid_url("https://example.com/hook#fragment", TEST_URL_MAX_LEN));
    TEST_ASSERT_FALSE(WEBHOOK_ALERT_UTILS_is_valid_url("https://example.com/\xC3\xA9", TEST_URL_MAX_LEN));
}

TEST_CASE("Webhook URLs reject malformed hostnames", "[webhook_alert_utils]")
{
    TEST_ASSERT_FALSE(WEBHOOK_ALERT_UTILS_is_valid_url("https://localhost/hook", TEST_URL_MAX_LEN));
    TEST_ASSERT_FALSE(WEBHOOK_ALERT_UTILS_is_valid_url("https://-example.com/hook", TEST_URL_MAX_LEN));
    TEST_ASSERT_FALSE(WEBHOOK_ALERT_UTILS_is_valid_url("https://example-.com/hook", TEST_URL_MAX_LEN));
    TEST_ASSERT_FALSE(WEBHOOK_ALERT_UTILS_is_valid_url("https://example..com/hook", TEST_URL_MAX_LEN));
}

TEST_CASE("Webhook test results remain correlated to their request", "[webhook_alert_utils]")
{
    TEST_ASSERT_FALSE(WEBHOOK_ALERT_UTILS_result_matches(2, 1));
    TEST_ASSERT_TRUE(WEBHOOK_ALERT_UTILS_result_matches(2, 2));
    TEST_ASSERT_FALSE(WEBHOOK_ALERT_UTILS_result_matches(0, 0));
}

TEST_CASE("Webhook result wait retains its original deadline", "[webhook_alert_utils]")
{
    TEST_ASSERT_EQUAL_UINT32(100, WEBHOOK_ALERT_UTILS_remaining_ticks(1000, 1000, 100));
    TEST_ASSERT_EQUAL_UINT32(40, WEBHOOK_ALERT_UTILS_remaining_ticks(1000, 1060, 100));
    TEST_ASSERT_EQUAL_UINT32(0, WEBHOOK_ALERT_UTILS_remaining_ticks(1000, 1100, 100));
    TEST_ASSERT_EQUAL_UINT32(2, WEBHOOK_ALERT_UTILS_remaining_ticks(UINT32_MAX - 2, 0, 5));
}

TEST_CASE("Webhook request deadline is absolute despite partial progress", "[webhook_alert_utils]")
{
    const int64_t request_start_us = 1000000;

    TEST_ASSERT_FALSE(WEBHOOK_ALERT_UTILS_deadline_expired(request_start_us, 5999000, 10000));
    TEST_ASSERT_FALSE(WEBHOOK_ALERT_UTILS_deadline_expired(request_start_us, 10999000, 10000));
    TEST_ASSERT_TRUE(WEBHOOK_ALERT_UTILS_deadline_expired(request_start_us, 11000000, 10000));
    TEST_ASSERT_TRUE(WEBHOOK_ALERT_UTILS_deadline_expired(request_start_us, 16000000, 10000));
}
