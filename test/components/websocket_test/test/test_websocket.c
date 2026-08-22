#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "unity.h"
#ifndef UNIT_TESTING
#define UNIT_TESTING 1
#endif
#include "websocket_internal.h"

TEST_CASE("WebSocket origin must match request host", "[websocket]")
{
    // Valid matching cases
    TEST_ASSERT_TRUE(websocket_origin_matches_host("http://192.168.1.42", "192.168.1.42"));
    TEST_ASSERT_TRUE(websocket_origin_matches_host("http://bitaxe.local", "bitaxe.local"));
    TEST_ASSERT_TRUE(websocket_origin_matches_host("https://BITAXE.local", "bitaxe.LOCAL"));
    TEST_ASSERT_TRUE(websocket_origin_matches_host("http://bitaxe.local:8080", "bitaxe.local:8080"));
    TEST_ASSERT_TRUE(websocket_origin_matches_host("HTTP://BITAXE.LOCAL", "bitaxe.local"));
    TEST_ASSERT_TRUE(websocket_origin_matches_host("http://[fd00::1]:8080", "[fd00::1]:8080"));

    // Host mismatches and cross-origin attacks
    TEST_ASSERT_FALSE(websocket_origin_matches_host("http://evil.local", "bitaxe.local"));
    TEST_ASSERT_FALSE(websocket_origin_matches_host("http://bitaxe.local:8080", "bitaxe.local"));
    TEST_ASSERT_FALSE(websocket_origin_matches_host("http://bitaxe.local", "bitaxe.local:8080"));

    // Untrusted/malicious origins & URL tricks
    TEST_ASSERT_FALSE(websocket_origin_matches_host("null", "bitaxe.local"));
    TEST_ASSERT_FALSE(websocket_origin_matches_host("file://bitaxe.local", "bitaxe.local"));
    TEST_ASSERT_FALSE(websocket_origin_matches_host("ftp://bitaxe.local", "bitaxe.local"));
    TEST_ASSERT_FALSE(websocket_origin_matches_host("ws://bitaxe.local", "bitaxe.local"));
    TEST_ASSERT_FALSE(websocket_origin_matches_host("http://bitaxe.local/", "bitaxe.local"));
    TEST_ASSERT_FALSE(websocket_origin_matches_host("http://bitaxe.local?query", "bitaxe.local"));
    TEST_ASSERT_FALSE(websocket_origin_matches_host("http://bitaxe.local#fragment", "bitaxe.local"));
    TEST_ASSERT_FALSE(websocket_origin_matches_host("http://bitaxe.local@example.com", "example.com"));
    TEST_ASSERT_FALSE(websocket_origin_matches_host("http://bitaxe.local.attacker.com", "bitaxe.local"));
    TEST_ASSERT_FALSE(websocket_origin_matches_host("http://bitaxe.local:invalid", "bitaxe.local"));
    TEST_ASSERT_FALSE(websocket_origin_matches_host("http://", "bitaxe.local"));
    TEST_ASSERT_FALSE(websocket_origin_matches_host("bitaxe.local", "bitaxe.local"));

    // Edge cases and NULL pointers
    TEST_ASSERT_FALSE(websocket_origin_matches_host(NULL, "bitaxe.local"));
    TEST_ASSERT_FALSE(websocket_origin_matches_host("http://bitaxe.local", NULL));
    TEST_ASSERT_FALSE(websocket_origin_matches_host("http://bitaxe.local", ""));
    TEST_ASSERT_FALSE(websocket_origin_matches_host("", "bitaxe.local"));
}

TEST_CASE("WebSocket client capacity tracking", "[websocket]")
{
    // After initialization, slots are empty
    websocket_init(NULL);
    TEST_ASSERT_TRUE(websocket_has_free_slot());
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        websocket_add_client(99, (WebSocketClientType)WS_TYPE_MAX));
    TEST_ASSERT_TRUE(websocket_has_free_slot());

    // Add maximum clients
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, websocket_add_client(100 + i, WS_TYPE_API));
    }

    // Capacity is now full
    TEST_ASSERT_FALSE(websocket_has_free_slot());

    // Adding 11th client fails
    TEST_ASSERT_EQUAL(ESP_FAIL, websocket_add_client(999, WS_TYPE_API));

    // Reinitialization resets both the slots and their per-type counts.
    websocket_init(NULL);
    TEST_ASSERT_TRUE(websocket_has_free_slot());
    TEST_ASSERT_EQUAL_INT(0, websocket_get_active_client_count(WS_TYPE_API));

    TEST_ASSERT_EQUAL(ESP_OK, websocket_add_client(100, WS_TYPE_API));
    websocket_remove_client(100);
    TEST_ASSERT_TRUE(websocket_has_free_slot());
}
