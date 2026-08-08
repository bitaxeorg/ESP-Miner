#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "unity.h"
#ifndef UNIT_TESTING
#define UNIT_TESTING 1
#endif
#include "websocket_internal.h"

TEST_CASE("WebSocket payload limit has strict boundary", "[websocket]")
{
    TEST_ASSERT_TRUE(websocket_payload_fits(0));
    TEST_ASSERT_TRUE(websocket_payload_fits(WS_MAX_WEBSOCKET_PAYLOAD_SIZE));
    TEST_ASSERT_FALSE(websocket_payload_fits(WS_MAX_WEBSOCKET_PAYLOAD_SIZE + 1U));
    TEST_ASSERT_FALSE(websocket_payload_fits(SIZE_MAX));
}

TEST_CASE("WebSocket origin must match request host", "[websocket]")
{
    // Valid matching cases
    TEST_ASSERT_TRUE(websocket_origin_matches_host("http://192.168.1.42", "192.168.1.42"));
    TEST_ASSERT_TRUE(websocket_origin_matches_host("http://bitaxe.local", "bitaxe.local"));
    TEST_ASSERT_TRUE(websocket_origin_matches_host("https://BITAXE.local", "bitaxe.LOCAL"));
    TEST_ASSERT_TRUE(websocket_origin_matches_host("http://bitaxe.local:8080", "bitaxe.local:8080"));

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
    TEST_ASSERT_FALSE(websocket_origin_matches_host("http://bitaxe.local@example.com", "example.com"));
    TEST_ASSERT_FALSE(websocket_origin_matches_host("http://bitaxe.local.attacker.com", "bitaxe.local"));

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

    // Add maximum clients
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, websocket_add_client(100 + i, WS_TYPE_API));
    }

    // Capacity is now full
    TEST_ASSERT_FALSE(websocket_has_free_slot());

    // Adding 11th client fails
    TEST_ASSERT_EQUAL(ESP_FAIL, websocket_add_client(999, WS_TYPE_API));

    // Remove 1 client and check capacity opens up
    websocket_remove_client(100);
    TEST_ASSERT_TRUE(websocket_has_free_slot());

    // Cleanup remaining clients
    for (int i = 1; i < MAX_WEBSOCKET_CLIENTS; i++) {
        websocket_remove_client(100 + i);
    }
}
