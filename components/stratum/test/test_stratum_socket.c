#include "unity.h"
#include "stratum_socket.h"

TEST_CASE("stratum_socket detects IP literals", "[stratum_socket]")
{
    TEST_ASSERT_TRUE(stratum_socket_is_ip_literal("192.0.2.10"));
    TEST_ASSERT_TRUE(stratum_socket_is_ip_literal("2001:db8::1"));
    TEST_ASSERT_TRUE(stratum_socket_is_ip_literal("[2001:db8::1]"));
    TEST_ASSERT_TRUE(stratum_socket_is_ip_literal("fe80::1"));

    TEST_ASSERT_FALSE(stratum_socket_is_ip_literal("pool.example.com"));
    TEST_ASSERT_FALSE(stratum_socket_is_ip_literal("[pool.example.com]"));
    TEST_ASSERT_FALSE(stratum_socket_is_ip_literal("[]"));
    TEST_ASSERT_FALSE(stratum_socket_is_ip_literal(""));
    TEST_ASSERT_FALSE(stratum_socket_is_ip_literal(NULL));
}

TEST_CASE("stratum_socket resolves IP literals without DNS", "[stratum_socket]")
{
    stratum_connection_info_t info;

    TEST_ASSERT_EQUAL(ESP_OK, stratum_socket_resolve("192.0.2.10", 3333, AF_UNSPEC, &info));
    TEST_ASSERT_EQUAL(AF_INET, info.addr_family);
    TEST_ASSERT_EQUAL_STRING("192.0.2.10", info.host_ip);

    const uint8_t expected[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01};
    const char *ipv6_literals[] = {"2001:db8::1", "[2001:db8::1]"};
    for (size_t i = 0; i < 2; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, stratum_socket_resolve(ipv6_literals[i], 3333, AF_UNSPEC, &info));
        TEST_ASSERT_EQUAL(AF_INET6, info.addr_family);
        const struct sockaddr_in6 *addr6 = (const struct sockaddr_in6 *)&info.dest_addr;
        TEST_ASSERT_EQUAL_UINT16(htons(3333), addr6->sin6_port);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, &addr6->sin6_addr, sizeof(expected));
        TEST_ASSERT_NULL(strchr(info.host_ip, '['));
    }
}
