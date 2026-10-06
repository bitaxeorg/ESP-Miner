#include "stratum_protocol.h"

#include <string.h>

stratum_protocol_t stratum_protocol_from_string(const char *value)
{
    if (value == NULL) {
        return STRATUM_PROTOCOL_UNKNOWN;
    }
    if (strcmp(value, STRATUM_V1) == 0) {
        return STRATUM_PROTOCOL_V1;
    }
    if (strcmp(value, STRATUM_V2) == 0) {
        return STRATUM_PROTOCOL_V2;
    }
    return STRATUM_PROTOCOL_UNKNOWN;
}
const char *stratum_protocol_to_string(stratum_protocol_t protocol)
{
    switch (protocol) {
        case STRATUM_PROTOCOL_V1:
            return STRATUM_V1;
        case STRATUM_PROTOCOL_V2:
            return STRATUM_V2;
        case STRATUM_PROTOCOL_UNKNOWN:
        default:
            return "unknown";
    }
}
