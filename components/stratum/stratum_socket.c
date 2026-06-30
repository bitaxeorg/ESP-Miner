#include "stratum_socket.h"

#include "esp_log.h"
#include "esp_netif.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "stratum_socket";

esp_err_t stratum_socket_resolve(const char *hostname, uint16_t port, stratum_connection_info_t *conn_info)
{
    // Input validation
    if (hostname == NULL || conn_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (port == 0) {
        ESP_LOGE(TAG, "Invalid port: 0");
        return ESP_ERR_INVALID_ARG;
    }

    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%u", port);

    ESP_LOGD(TAG, "Resolving address for %s:%u", hostname, port);

    struct addrinfo hints = {
        .ai_family   = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP,
        .ai_flags    = AI_NUMERICSERV
    };

    // getaddrinfo() maps to esp_getaddrinfo() when CONFIG_LWIP_USE_ESP_GETADDRINFO
    // is enabled (as it is in the firmware), which resolves AF_UNSPEC into both
    // IPv4 and IPv6. Using the standard name keeps this component buildable under
    // the default lwip config too (e.g. the unit-test build).
    struct addrinfo *res = NULL;
    int gai_err = getaddrinfo(hostname, port_str, &hints, &res);
    if (gai_err != 0 || res == NULL) {
        ESP_LOGE(TAG, "DNS resolution failed for %s:%u (error: %d)", hostname, port, gai_err);
        return ESP_ERR_NOT_FOUND;
    }

    // Initialize connection info
    memset(conn_info, 0, sizeof(*conn_info));
    conn_info->addr_family = AF_UNSPEC;

    // Preferred order: IPv4 first, then IPv6
    const int preferred_families[] = { AF_INET, AF_INET6 };
    const size_t num_families = sizeof(preferred_families) / sizeof(preferred_families[0]);

    const struct addrinfo *selected = NULL;

    for (size_t i = 0; i < num_families && selected == NULL; i++) {
        int family = preferred_families[i];

        for (const struct addrinfo *p = res; p != NULL; p = p->ai_next) {
            if (p->ai_family == family) {
                selected = p;
                break;
            }
        }
    }

    if (selected == NULL) {
        ESP_LOGE(TAG, "No supported address family (IPv4 or IPv6) found for %s", hostname);
        freeaddrinfo(res);
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Copy selected address
    memcpy(&conn_info->dest_addr, selected->ai_addr, selected->ai_addrlen);
    conn_info->addrlen     = selected->ai_addrlen;
    conn_info->addr_family = selected->ai_family;
    conn_info->ip_protocol = (selected->ai_family == AF_INET) ? IPPROTO_IP : IPPROTO_IPV6;

    // Handle IPv6 link-local scope ID if needed
    if (selected->ai_family == AF_INET6) {
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&conn_info->dest_addr;

        if (IN6_IS_ADDR_LINKLOCAL(&addr6->sin6_addr)) {
            if (addr6->sin6_scope_id == 0) {
                ESP_LOGW(TAG, "Link-local IPv6 address without scope ID - attempting to set from WiFi STA interface");

                esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                if (netif) {
                    int index = esp_netif_get_netif_impl_index(netif);
                    if (index >= 0) {
                        addr6->sin6_scope_id = (uint32_t)index;
                        ESP_LOGI(TAG, "Set IPv6 scope_id to interface index: %lu", (unsigned long)addr6->sin6_scope_id);
                    } else {
                        ESP_LOGW(TAG, "Failed to get valid interface index for WIFI_STA_DEF");
                    }
                } else {
                    ESP_LOGW(TAG, "Could not get netif handle for WIFI_STA_DEF");
                }
            } else {
                ESP_LOGI(TAG, "Link-local IPv6 address with existing scope_id: %lu", (unsigned long)addr6->sin6_scope_id);
            }
        }
    }

    const void *src_addr;
    int af = conn_info->addr_family;

    if (af == AF_INET) {
        struct sockaddr_in *addr4 = (struct sockaddr_in *)&conn_info->dest_addr;
        src_addr = &addr4->sin_addr;
    } else {
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&conn_info->dest_addr;
        src_addr = &addr6->sin6_addr;
    }

    // Convert resolved address to string for logging and storage
    if (inet_ntop(af, src_addr, conn_info->host_ip, sizeof(conn_info->host_ip)) == NULL) {
        ESP_LOGW(TAG, "inet_ntop failed (errno: %d)", errno);
        snprintf(conn_info->host_ip, sizeof(conn_info->host_ip), "[invalid %s addr]",
                 (af == AF_INET) ? "IPv4" : "IPv6");
    } else if (af == AF_INET6) {
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&conn_info->dest_addr;
        if (IN6_IS_ADDR_LINKLOCAL(&addr6->sin6_addr) && addr6->sin6_scope_id != 0) {
            char zone[16];
            snprintf(zone, sizeof(zone), "%%%" PRIu32, addr6->sin6_scope_id);
            strncat(conn_info->host_ip, zone,
                    sizeof(conn_info->host_ip) - strlen(conn_info->host_ip) - 1);
            // Ensure null termination
            conn_info->host_ip[sizeof(conn_info->host_ip) - 1] = '\0';
        }
    }

    ESP_LOGI(TAG, "Resolved %s:%u → %s", hostname, port, conn_info->host_ip);

    freeaddrinfo(res);
    return ESP_OK;
}

void stratum_socket_set_options(esp_transport_handle_t transport)
{
    int sock = esp_transport_get_socket(transport);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to get socket from transport");
        return;
    }

    // Send and receive timeouts
    struct timeval snd_timeout = { .tv_sec = 5, .tv_usec = 0 };
    struct timeval rcv_timeout = { .tv_sec = 60 * 3, .tv_usec = 0 };
    if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &snd_timeout, sizeof(snd_timeout)) < 0) {
        ESP_LOGE(TAG, "Failed to set SO_SNDTIMEO");
    }
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &rcv_timeout, sizeof(rcv_timeout)) < 0) {
        ESP_LOGE(TAG, "Failed to set SO_RCVTIMEO");
    }

    // Disable Nagle's algorithm so share submits are sent immediately. Stratum
    // frames can be written in more than one segment; with Nagle on, later
    // segments are held until earlier ones are ACKed, which collides with the
    // pool's delayed-ACK and adds latency per submit.
    int nodelay = 1;
    if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
        ESP_LOGE(TAG, "Failed to set TCP_NODELAY");
    }

    // Keepalive
    int keepalive = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)) < 0) {
        ESP_LOGE(TAG, "Failed to set SO_KEEPALIVE");
    }
    int keepidle = 60;  // seconds before sending keepalive probes
    int keepintvl = 10; // seconds between keepalive probes
    int keepcnt = 3;    // number of keepalive probes before dropping
    if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle)) < 0) {
        ESP_LOGE(TAG, "Failed to set TCP_KEEPIDLE");
    }
    if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl)) < 0) {
        ESP_LOGE(TAG, "Failed to set TCP_KEEPINTVL");
    }
    if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt)) < 0) {
        ESP_LOGE(TAG, "Failed to set TCP_KEEPCNT");
    }
}
