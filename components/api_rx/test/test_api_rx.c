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
    TEST_ASSERT_TRUE(api_rx_websocket_origin_matches_host(
        "HTTP://BITAXE.LOCAL", "bitaxe.local"));
    TEST_ASSERT_TRUE(api_rx_websocket_origin_matches_host(
        "http://[fd00::1]:8080", "[fd00::1]:8080"));

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
        "http://bitaxe.local?query", "bitaxe.local"));
    TEST_ASSERT_FALSE(api_rx_websocket_origin_matches_host(
        "http://bitaxe.local#fragment", "bitaxe.local"));
    TEST_ASSERT_FALSE(api_rx_websocket_origin_matches_host(
        "http://bitaxe.local@example.com", "example.com"));
    TEST_ASSERT_FALSE(api_rx_websocket_origin_matches_host(
        "http://bitaxe.local:invalid", "bitaxe.local"));
    TEST_ASSERT_FALSE(api_rx_websocket_origin_matches_host(
        "http://", "bitaxe.local"));
    TEST_ASSERT_FALSE(api_rx_websocket_origin_matches_host(
        "bitaxe.local", "bitaxe.local"));
    TEST_ASSERT_FALSE(api_rx_websocket_origin_matches_host(NULL, "bitaxe.local"));
    TEST_ASSERT_FALSE(api_rx_websocket_origin_matches_host("http://bitaxe.local", NULL));
    TEST_ASSERT_FALSE(api_rx_websocket_origin_matches_host("http://bitaxe.local", ""));
}

TEST_CASE("LAN address validation handles IPv4 and IPv6", "[api_rx]")
{
    const unsigned char private_ipv4[4] = {192, 168, 1, 42};
    const unsigned char public_ipv4[4] = {8, 8, 8, 8};
    const unsigned char mapped_private_ipv6[16] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 10, 0, 0, 2
    };
    const unsigned char mapped_public_ipv6[16] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 8, 8, 8, 8
    };
    const unsigned char unique_local_ipv6[16] = {
        0xfd, 0x12, 0x34, 0x56, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1
    };
    const unsigned char link_local_ipv6[16] = {
        0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1
    };
    const unsigned char loopback_ipv6[16] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1
    };
    const unsigned char global_ipv6[16] = {
        0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0, 0, 0, 0, 0, 0, 0, 0, 0x88, 0x88
    };
    const unsigned char global_ipv6_with_private_tail[16] = {
        0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 1, 42
    };
    const unsigned char deprecated_site_local_ipv6[16] = {
        0xfe, 0xc0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1
    };

    TEST_ASSERT_TRUE(api_rx_ipv4_address_is_lan(private_ipv4));
    TEST_ASSERT_FALSE(api_rx_ipv4_address_is_lan(public_ipv4));
    TEST_ASSERT_TRUE(api_rx_ipv6_address_is_lan(mapped_private_ipv6));
    TEST_ASSERT_FALSE(api_rx_ipv6_address_is_lan(mapped_public_ipv6));
    TEST_ASSERT_TRUE(api_rx_ipv6_address_is_lan(unique_local_ipv6));
    TEST_ASSERT_TRUE(api_rx_ipv6_address_is_lan(link_local_ipv6));
    TEST_ASSERT_TRUE(api_rx_ipv6_address_is_lan(loopback_ipv6));
    TEST_ASSERT_FALSE(api_rx_ipv6_address_is_lan(global_ipv6));
    TEST_ASSERT_FALSE(api_rx_ipv6_address_is_lan(global_ipv6_with_private_tail));
    TEST_ASSERT_FALSE(api_rx_ipv6_address_is_lan(deprecated_site_local_ipv6));
}

TEST_CASE("HTTP origins reject malformed DNS labels", "[api_rx]")
{
    TEST_ASSERT_FALSE(api_rx_origin_is_lan("http://foo..local"));
    TEST_ASSERT_FALSE(api_rx_origin_is_lan("http://foo-.local"));
    TEST_ASSERT_FALSE(api_rx_origin_is_lan(
        "http://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.local"));
}

TEST_CASE("HTTP origins must identify a LAN host", "[api_rx]")
{
    TEST_ASSERT_TRUE(api_rx_origin_is_lan("http://192.168.1.42"));
    TEST_ASSERT_TRUE(api_rx_origin_is_lan("https://10.0.0.2:8443"));
    TEST_ASSERT_TRUE(api_rx_origin_is_lan("http://bitaxe.local"));
    TEST_ASSERT_TRUE(api_rx_origin_is_lan("http://bitaxe"));
    TEST_ASSERT_TRUE(api_rx_origin_is_lan("http://[fd12:3456::1]:8080"));
    TEST_ASSERT_TRUE(api_rx_origin_is_lan("http://[fe80::1]"));

    TEST_ASSERT_FALSE(api_rx_origin_is_lan("https://example.com"));
    TEST_ASSERT_FALSE(api_rx_origin_is_lan("http://8.8.8.8"));
    TEST_ASSERT_FALSE(api_rx_origin_is_lan("http://[2001:4860:4860::8888]"));
    TEST_ASSERT_FALSE(api_rx_origin_is_lan("file://bitaxe.local"));
    TEST_ASSERT_FALSE(api_rx_origin_is_lan("http://bitaxe.local/path"));
    TEST_ASSERT_FALSE(api_rx_origin_is_lan("http://user@bitaxe.local"));
    TEST_ASSERT_FALSE(api_rx_origin_is_lan("http://bitaxe.local:invalid"));
}

TEST_CASE("IPv4 LAN classification covers every range boundary", "[api_rx]")
{
    static const struct {
        unsigned char address[4];
        bool expected;
    } cases[] = {
        {{9, 255, 255, 255}, false}, {{10, 0, 0, 0}, true},
        {{10, 255, 255, 255}, true}, {{11, 0, 0, 0}, false},
        {{127, 0, 0, 0}, true}, {{127, 255, 255, 255}, true},
        {{128, 0, 0, 0}, false}, {{172, 15, 255, 255}, false},
        {{172, 16, 0, 0}, true}, {{172, 31, 255, 255}, true},
        {{172, 32, 0, 0}, false}, {{169, 253, 255, 255}, false},
        {{169, 254, 0, 0}, true}, {{169, 254, 255, 255}, true},
        {{169, 255, 0, 0}, false}, {{192, 167, 255, 255}, false},
        {{192, 168, 0, 0}, true}, {{192, 168, 255, 255}, true},
        {{192, 169, 0, 0}, false},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TEST_ASSERT_EQUAL_INT(cases[i].expected,
                              api_rx_ipv4_address_is_lan(cases[i].address));
    }
    TEST_ASSERT_FALSE(api_rx_ipv4_address_is_lan(NULL));
}

TEST_CASE("IPv6 LAN classification covers prefix boundaries", "[api_rx]")
{
    const unsigned char below_unique_local[16] = {0xfb};
    const unsigned char unique_local_start[16] = {0xfc};
    const unsigned char unique_local_end[16] = {0xfd, 0xff};
    const unsigned char below_link_local[16] = {0xfe, 0x7f};
    const unsigned char link_local_start[16] = {0xfe, 0x80};
    const unsigned char link_local_end[16] = {0xfe, 0xbf};
    const unsigned char above_link_local[16] = {0xfe, 0xc0};
    const unsigned char unspecified[16] = {0};

    TEST_ASSERT_FALSE(api_rx_ipv6_address_is_lan(below_unique_local));
    TEST_ASSERT_TRUE(api_rx_ipv6_address_is_lan(unique_local_start));
    TEST_ASSERT_TRUE(api_rx_ipv6_address_is_lan(unique_local_end));
    TEST_ASSERT_FALSE(api_rx_ipv6_address_is_lan(below_link_local));
    TEST_ASSERT_TRUE(api_rx_ipv6_address_is_lan(link_local_start));
    TEST_ASSERT_TRUE(api_rx_ipv6_address_is_lan(link_local_end));
    TEST_ASSERT_FALSE(api_rx_ipv6_address_is_lan(above_link_local));
    TEST_ASSERT_FALSE(api_rx_ipv6_address_is_lan(unspecified));
    TEST_ASSERT_FALSE(api_rx_ipv6_address_is_lan(NULL));
}

TEST_CASE("HTTP origin parser covers authority boundaries", "[api_rx]")
{
    TEST_ASSERT_TRUE(api_rx_origin_is_lan("HTTP://BITAXE.LOCAL"));
    TEST_ASSERT_TRUE(api_rx_origin_is_lan("http://192.168.1.42:65535"));
    TEST_ASSERT_FALSE(api_rx_origin_is_lan("http://192.168.1.42:"));
    TEST_ASSERT_FALSE(api_rx_origin_is_lan("http://192.168.1.42:65536"));
    TEST_ASSERT_FALSE(api_rx_origin_is_lan("http://"));
    TEST_ASSERT_FALSE(api_rx_origin_is_lan("http://[fe80::1"));
    TEST_ASSERT_FALSE(api_rx_origin_is_lan("http://fe80::1"));
    TEST_ASSERT_FALSE(api_rx_origin_is_lan("http://bitaxe.local?query"));
    TEST_ASSERT_FALSE(api_rx_origin_is_lan("http://bitaxe.local#fragment"));
    TEST_ASSERT_FALSE(api_rx_origin_is_lan(NULL));
}
