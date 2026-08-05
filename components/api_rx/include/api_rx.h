#ifndef API_RX_H_
#define API_RX_H_

#include <stdbool.h>
#include <stddef.h>

#define API_RX_MAX_WEBSOCKET_PAYLOAD_SIZE 1024U

bool api_rx_websocket_payload_fits(size_t payload_len);
bool api_rx_websocket_origin_matches_host(const char *origin, const char *host);

#endif /* API_RX_H_ */
