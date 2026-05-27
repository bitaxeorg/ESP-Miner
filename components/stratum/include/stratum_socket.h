#ifndef STRATUM_SOCKET_H_
#define STRATUM_SOCKET_H_

#include <stdint.h>
#include "esp_err.h"
#include "esp_transport.h"
#include <lwip/sockets.h>
#include <lwip/netdb.h>

// Resolved pool address, including the textual host_ip (with IPv6 zone id when
// applicable) used for connecting and logging.
typedef struct {
    struct sockaddr_storage dest_addr;  // IPv4 or IPv6 address (with scope_id for IPv6)
    socklen_t addrlen;
    int addr_family;
    int ip_protocol;
    char host_ip[INET6_ADDRSTRLEN + 16];  // IPv6 address + zone identifier (e.g., "fe80::1%wlan0")
} stratum_connection_info_t;

// Resolve a pool hostname:port into conn_info, preferring IPv4 then IPv6 and
// handling IPv6 link-local scope ids. Returns ESP_OK on success. Used by both
// the SV1 and SV2 tasks so DNS resolution stays non-blocking (the resolved IP
// is passed to esp_transport_connect instead of the hostname).
esp_err_t stratum_socket_resolve(const char *hostname, uint16_t port, stratum_connection_info_t *conn_info);

// Apply the common pool-socket options (timeouts, TCP_NODELAY, keepalive) used
// by both the SV1 and SV2 stratum tasks.
void stratum_socket_set_options(esp_transport_handle_t transport);

#endif /* STRATUM_SOCKET_H_ */
