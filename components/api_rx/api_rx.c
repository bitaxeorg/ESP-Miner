#include <ctype.h>
#include <string.h>
#include <strings.h>

#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "api_rx.h"

bool api_rx_ipv4_address_is_lan(const unsigned char address[4])
{
    if (address == NULL) {
        return false;
    }

    return address[0] == 10 ||
           (address[0] == 172 && address[1] >= 16 && address[1] <= 31) ||
           (address[0] == 192 && address[1] == 168) ||
           address[0] == 127 ||
           (address[0] == 169 && address[1] == 254);
}

bool api_rx_ipv6_address_is_lan(const unsigned char address[16])
{
    if (address == NULL) {
        return false;
    }

    static const unsigned char ipv4_mapped_prefix[12] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff
    };
    static const unsigned char loopback[16] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1
    };

    if (memcmp(address, ipv4_mapped_prefix, sizeof(ipv4_mapped_prefix)) == 0) {
        return api_rx_ipv4_address_is_lan(address + sizeof(ipv4_mapped_prefix));
    }

    return (address[0] & 0xfeU) == 0xfcU ||
           (address[0] == 0xfeU && (address[1] & 0xc0U) == 0x80U) ||
           memcmp(address, loopback, sizeof(loopback)) == 0;
}

static bool api_rx_origin_port_is_valid(const char *port)
{
    if (port == NULL || *port == '\0') {
        return false;
    }

    unsigned long value = 0;
    for (const unsigned char *p = (const unsigned char *)port; *p != '\0'; p++) {
        if (!isdigit(*p)) {
            return false;
        }
        value = value * 10UL + (unsigned long)(*p - '0');
        if (value > 65535UL) {
            return false;
        }
    }
    return true;
}

static bool api_rx_local_hostname_is_valid(const char *host)
{
    if (host == NULL || *host == '\0') {
        return false;
    }

    size_t host_len = strlen(host);
    if (host_len > 253) {
        return false;
    }

    const unsigned char *label = (const unsigned char *)host;
    for (const unsigned char *p = label;; p++) {
        if (*p != '\0' && *p != '.') {
            if (!isalnum(*p) && *p != '-') {
                return false;
            }
            continue;
        }

        size_t label_len = (size_t)(p - label);
        if (label_len == 0 || label_len > 63 ||
            label[0] == '-' || label[label_len - 1] == '-') {
            return false;
        }

        if (*p == '\0') {
            break;
        }
        label = p + 1;
    }

    if (strchr(host, '.') == NULL) {
        return true;
    }

    static const char local_suffix[] = ".local";
    size_t suffix_len = sizeof(local_suffix) - 1;
    return host_len > suffix_len &&
           strcasecmp(host + host_len - suffix_len, local_suffix) == 0;
}

bool api_rx_origin_is_lan(const char *origin)
{
    if (origin == NULL) {
        return false;
    }

    const char *authority = NULL;
    static const char http_prefix[] = "http://";
    static const char https_prefix[] = "https://";

    if (strncasecmp(origin, http_prefix, sizeof(http_prefix) - 1) == 0) {
        authority = origin + sizeof(http_prefix) - 1;
    } else if (strncasecmp(origin, https_prefix, sizeof(https_prefix) - 1) == 0) {
        authority = origin + sizeof(https_prefix) - 1;
    } else {
        return false;
    }

    size_t authority_len = strcspn(authority, "/?#");
    if (authority_len == 0 || authority[authority_len] != '\0' ||
        authority_len >= 256 || memchr(authority, '@', authority_len) != NULL) {
        return false;
    }

    char host[256];
    if (authority[0] == '[') {
        const char *closing_bracket = memchr(authority, ']', authority_len);
        if (closing_bracket == NULL) {
            return false;
        }

        size_t host_len = (size_t)(closing_bracket - authority - 1);
        const char *remainder = closing_bracket + 1;
        if (host_len == 0 || host_len >= sizeof(host) ||
            (*remainder != '\0' &&
             (*remainder != ':' || !api_rx_origin_port_is_valid(remainder + 1)))) {
            return false;
        }

        memcpy(host, authority + 1, host_len);
        host[host_len] = '\0';

        unsigned char ipv6[16];
        return inet_pton(AF_INET6, host, ipv6) == 1 &&
               api_rx_ipv6_address_is_lan(ipv6);
    }

    const char *colon = memchr(authority, ':', authority_len);
    size_t host_len = colon ? (size_t)(colon - authority) : authority_len;
    if (host_len == 0 || host_len >= sizeof(host) ||
        (colon != NULL && !api_rx_origin_port_is_valid(colon + 1))) {
        return false;
    }

    memcpy(host, authority, host_len);
    host[host_len] = '\0';

    unsigned char ipv4[4];
    if (inet_pton(AF_INET, host, ipv4) == 1) {
        return api_rx_ipv4_address_is_lan(ipv4);
    }

    return api_rx_local_hostname_is_valid(host);
}
