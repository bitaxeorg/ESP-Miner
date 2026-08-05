#include <stdint.h>

#include "api_rx.h"
#include "unity.h"

TEST_CASE("WebSocket payload limit has strict boundary", "[api_rx]")
{
    TEST_ASSERT_TRUE(api_rx_websocket_payload_fits(0));
    TEST_ASSERT_TRUE(api_rx_websocket_payload_fits(API_RX_MAX_WEBSOCKET_PAYLOAD_SIZE));
    TEST_ASSERT_FALSE(api_rx_websocket_payload_fits(API_RX_MAX_WEBSOCKET_PAYLOAD_SIZE + 1U));
    TEST_ASSERT_FALSE(api_rx_websocket_payload_fits(SIZE_MAX));
}

TEST_CASE("WebSocket origin must match request host", "[api_rx]")
{
    TEST_ASSERT_TRUE(api_rx_websocket_origin_matches_host(
        "http://192.168.1.42", "192.168.1.42"));
    TEST_ASSERT_TRUE(api_rx_websocket_origin_matches_host(
        "http://bitaxe.local", "bitaxe.local"));
    TEST_ASSERT_TRUE(api_rx_websocket_origin_matches_host(
        "https://BITAXE.local", "bitaxe.LOCAL"));
    TEST_ASSERT_TRUE(api_rx_websocket_origin_matches_host(
        "http://bitaxe.local:8080", "bitaxe.local:8080"));

    TEST_ASSERT_FALSE(api_rx_websocket_origin_matches_host(
        "http://evil.local", "bitaxe.local"));
    TEST_ASSERT_FALSE(api_rx_websocket_origin_matches_host(
        "http://bitaxe.local:8080", "bitaxe.local"));
    TEST_ASSERT_FALSE(api_rx_websocket_origin_matches_host(
        "null", "bitaxe.local"));
    TEST_ASSERT_FALSE(api_rx_websocket_origin_matches_host(
        "file://bitaxe.local", "bitaxe.local"));
    TEST_ASSERT_FALSE(api_rx_websocket_origin_matches_host(
        "http://bitaxe.local/", "bitaxe.local"));
    TEST_ASSERT_FALSE(api_rx_websocket_origin_matches_host(
        "http://bitaxe.local@example.com", "example.com"));
    TEST_ASSERT_FALSE(api_rx_websocket_origin_matches_host(NULL, "bitaxe.local"));
    TEST_ASSERT_FALSE(api_rx_websocket_origin_matches_host("http://bitaxe.local", NULL));
}
