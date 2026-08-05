#include <string.h>
#include <strings.h>

#include "api_rx.h"

bool api_rx_websocket_payload_fits(size_t payload_len)
{
    return payload_len <= API_RX_MAX_WEBSOCKET_PAYLOAD_SIZE;
}

bool api_rx_websocket_origin_matches_host(const char *origin, const char *host)
{
    if (origin == NULL || host == NULL || host[0] == '\0') {
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
    if (authority_len == 0 || authority[authority_len] != '\0') {
        return false;
    }

    size_t host_len = strlen(host);
    return authority_len == host_len && strncasecmp(authority, host, host_len) == 0;
}
