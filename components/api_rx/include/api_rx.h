#ifndef API_RX_H_
#define API_RX_H_

#include <stdbool.h>
#include <stddef.h>

#define API_RX_MAX_WEBSOCKET_PAYLOAD_SIZE 1024U

bool api_rx_websocket_payload_fits(size_t payload_len);
bool api_rx_websocket_origin_matches_host(const char *origin, const char *host);
bool api_rx_ipv4_address_is_lan(const unsigned char address[4]);
bool api_rx_ipv6_address_is_lan(const unsigned char address[16]);
bool api_rx_origin_is_lan(const char *origin);

#endif /* API_RX_H_ */
