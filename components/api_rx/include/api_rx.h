#ifndef API_RX_H_
#define API_RX_H_

#include <stdbool.h>

bool api_rx_ipv4_address_is_lan(const unsigned char address[4]);
bool api_rx_ipv6_address_is_lan(const unsigned char address[16]);
bool api_rx_origin_is_lan(const char *origin);

#endif /* API_RX_H_ */
