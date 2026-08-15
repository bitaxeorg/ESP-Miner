#include "http_rx.h"
#include "unity.h"

TEST_CASE("HTTP body size policy preserves terminator space", "[http_rx]")
{
    TEST_ASSERT_EQUAL_INT(HTTP_RX_BODY_SIZE_INVALID,
                          http_rx_body_size_result(1, 1));
    TEST_ASSERT_EQUAL_INT(HTTP_RX_BODY_SIZE_EMPTY,
                          http_rx_body_size_result(0, 16));
    TEST_ASSERT_EQUAL_INT(HTTP_RX_BODY_SIZE_OK,
                          http_rx_body_size_result(15, 16));
    TEST_ASSERT_EQUAL_INT(HTTP_RX_BODY_SIZE_TOO_LARGE,
                          http_rx_body_size_result(16, 16));
    TEST_ASSERT_EQUAL_INT(HTTP_RX_BODY_SIZE_TOO_LARGE,
                          http_rx_body_size_result(SIZE_MAX, 16));
}

TEST_CASE("HTTP body read policy handles fragments and failures", "[http_rx]")
{
    size_t received_total = 0;
    TEST_ASSERT_EQUAL_INT(HTTP_RX_BODY_READ_CONTINUE,
        http_rx_body_read_update(5, &received_total, 2, -3, 5, 10));
    TEST_ASSERT_EQUAL_size_t(2, received_total);
    TEST_ASSERT_EQUAL_INT(HTTP_RX_BODY_READ_COMPLETE,
        http_rx_body_read_update(5, &received_total, 3, -3, 10, 10));
    TEST_ASSERT_EQUAL_size_t(5, received_total);

    received_total = 0;
    TEST_ASSERT_EQUAL_INT(HTTP_RX_BODY_READ_CONTINUE,
        http_rx_body_read_update(5, &received_total, -3, -3, 9, 10));
    TEST_ASSERT_EQUAL_INT(HTTP_RX_BODY_READ_TIMEOUT,
        http_rx_body_read_update(5, &received_total, -3, -3, 10, 10));
    TEST_ASSERT_EQUAL_INT(HTTP_RX_BODY_READ_INCOMPLETE,
        http_rx_body_read_update(5, &received_total, 0, -3, 1, 10));
    TEST_ASSERT_EQUAL_INT(HTTP_RX_BODY_READ_INCOMPLETE,
        http_rx_body_read_update(5, &received_total, 6, -3, 1, 10));
    TEST_ASSERT_EQUAL_INT(HTTP_RX_BODY_READ_INCOMPLETE,
        http_rx_body_read_update(5, NULL, 1, -3, 1, 10));

    received_total = 6;
    TEST_ASSERT_EQUAL_INT(HTTP_RX_BODY_READ_INCOMPLETE,
        http_rx_body_read_update(5, &received_total, 1, -3, 1, 10));
    TEST_ASSERT_EQUAL_size_t(6, received_total);

    received_total = 1;
    TEST_ASSERT_EQUAL_INT(HTTP_RX_BODY_READ_TIMEOUT,
        http_rx_body_read_update(5, &received_total, 1, -3, 10, 10));
}

TEST_CASE("Upload deadline policy covers exact boundaries", "[http_rx]")
{
    TEST_ASSERT_FALSE(http_rx_upload_deadline_expired(9, 0, 5, 10, 5));
    TEST_ASSERT_TRUE(http_rx_upload_deadline_expired(10, 0, 6, 10, 5));
    TEST_ASSERT_TRUE(http_rx_upload_deadline_expired(9, 0, 4, 10, 5));
}
