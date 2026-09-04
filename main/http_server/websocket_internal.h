#ifndef WEBSOCKET_INTERNAL_H_
#define WEBSOCKET_INTERNAL_H_

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_http_server.h"
#include "websocket.h"

#define WS_MAX_WEBSOCKET_PAYLOAD_SIZE 1024U
// Cap each copied value below the server's configured 1024-byte total
// request-header limit while keeping handshake stack use bounded.
#define WS_HANDSHAKE_HEADER_SIZE 256

#ifdef UNIT_TESTING
#define WEBSOCKET_STATIC
#else
#define WEBSOCKET_STATIC static
#endif

WEBSOCKET_STATIC bool websocket_origin_matches_host(const char *origin, const char *host);
WEBSOCKET_STATIC bool websocket_has_free_slot(void);
WEBSOCKET_STATIC esp_err_t websocket_origin_is_allowed(httpd_req_t *req);

#endif /* WEBSOCKET_INTERNAL_H_ */
