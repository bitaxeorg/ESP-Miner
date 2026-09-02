#include "stratum_socket.h"

#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "stratum_socket";

typedef struct {
    ip_addr_t ip;
    volatile int status; // 0 = pending, 1 = success, -1 = failed, 2 = abandoned
} dns_resolve_ctx_t;

static void dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *callback_arg)
{
    dns_resolve_ctx_t *ctx = (dns_resolve_ctx_t *)callback_arg;
    if (!ctx) return;

    if (ctx->status == 2) {
        // Context abandoned due to timeout, clean up memory
        free(ctx);
        return;
    }

    if (ipaddr) {
        ctx->ip = *ipaddr;
        ctx->status = 1;
    } else {
        ctx->status = -1;
    }
}

esp_err_t stratum_socket_resolve(const char *hostname, uint16_t port, stratum_connection_info_t *conn_info)
{
    // Input validation
    if (hostname == NULL || conn_info == NULL || port == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "Resolving address for %s:%u", hostname, port);

    dns_resolve_ctx_t *ctx = calloc(1, sizeof(dns_resolve_ctx_t));
    if (!ctx) {
        return ESP_ERR_NO_MEM;
    }

    err_t dns_err = dns_gethostbyname_addrtype(hostname, &ctx->ip, dns_found_cb, ctx, LWIP_DNS_ADDRTYPE_DEFAULT);

    if (dns_err == ERR_OK) {
        ctx->status = 1;
    } else if (dns_err == ERR_INPROGRESS) {
        int elapsed_ms = 0;
        while (ctx->status == 0 && elapsed_ms < 10000) {
            vTaskDelay(pdMS_TO_TICKS(50));
            elapsed_ms += 50;
        }
    }

    if (ctx->status != 1) {
        if (dns_err == ERR_INPROGRESS && ctx->status == 0) {
            ctx->status = 2; // Mark abandoned for callback cleanup
            ESP_LOGE(TAG, "DNS resolution timed out for %s", hostname);
            return ESP_ERR_TIMEOUT;
        }

        free(ctx);
        ESP_LOGE(TAG, "DNS resolution failed for %s (err %d)", hostname, (int)dns_err);
        return ESP_ERR_NOT_FOUND;
    }

    ip_addr_t resolved_ip = ctx->ip;
    free(ctx);

    // Initialize connection info from resolved_ip
    memset(conn_info, 0, sizeof(*conn_info));
    if (IP_IS_V4(&resolved_ip)) {
        struct sockaddr_in *addr4 = (struct sockaddr_in *)&conn_info->dest_addr;
        addr4->sin_family = AF_INET;
        addr4->sin_port = htons(port);
        addr4->sin_addr.s_addr = ip_2_ip4(&resolved_ip)->addr;
        conn_info->addrlen = sizeof(struct sockaddr_in);
        conn_info->addr_family = AF_INET;
        conn_info->ip_protocol = IPPROTO_IP;
        if (inet_ntop(AF_INET, &addr4->sin_addr, conn_info->host_ip, sizeof(conn_info->host_ip)) == NULL) {
            snprintf(conn_info->host_ip, sizeof(conn_info->host_ip), "[invalid IPv4 addr]");
        }
    } else {
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&conn_info->dest_addr;
        addr6->sin6_family = AF_INET6;
        addr6->sin6_port = htons(port);
        memcpy(&addr6->sin6_addr, ip_2_ip6(&resolved_ip)->addr, sizeof(addr6->sin6_addr));
        conn_info->addrlen = sizeof(struct sockaddr_in6);
        conn_info->addr_family = AF_INET6;
        conn_info->ip_protocol = IPPROTO_IPV6;

        // Handle IPv6 link-local scope ID if needed
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

        if (inet_ntop(AF_INET6, &addr6->sin6_addr, conn_info->host_ip, sizeof(conn_info->host_ip)) == NULL) {
            snprintf(conn_info->host_ip, sizeof(conn_info->host_ip), "[invalid IPv6 addr]");
        } else if (IN6_IS_ADDR_LINKLOCAL(&addr6->sin6_addr) && addr6->sin6_scope_id != 0) {
            char zone[16];
            snprintf(zone, sizeof(zone), "%%%" PRIu32, addr6->sin6_scope_id);
            strncat(conn_info->host_ip, zone, sizeof(conn_info->host_ip) - strlen(conn_info->host_ip) - 1);
            conn_info->host_ip[sizeof(conn_info->host_ip) - 1] = '\0';
        }
    }

    ESP_LOGI(TAG, "Resolved %s:%u → %s", hostname, port, conn_info->host_ip);
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

esp_err_t stratum_socket_connect_async(esp_transport_handle_t transport,
                                      const char *host_ip,
                                      uint16_t port,
                                      int timeout_ms,
                                      bool (*should_shutdown_fn)(void))
{
    if (!transport || !host_ip) {
        return ESP_ERR_INVALID_ARG;
    }

    if (should_shutdown_fn && should_shutdown_fn()) {
        return ESP_ERR_INVALID_STATE;
    }

    int connect_ret;
    while ((connect_ret = esp_transport_connect_async(transport, host_ip, port, timeout_ms)) == 0) {
        if (should_shutdown_fn && should_shutdown_fn()) {
            return ESP_ERR_INVALID_STATE;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (connect_ret == 1) {
        return ESP_OK;
    }

    return ESP_FAIL;
}

