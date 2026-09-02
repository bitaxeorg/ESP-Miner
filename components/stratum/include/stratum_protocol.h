#ifndef STRATUM_PROTOCOL_H_
#define STRATUM_PROTOCOL_H_

typedef enum {
    STRATUM_PROTOCOL_UNKNOWN = 0,
    STRATUM_PROTOCOL_V1 = 1,
    STRATUM_PROTOCOL_V2 = 2,
} stratum_protocol_t;

#define STRATUM_V1 "SV1"
#define STRATUM_V2 "SV2"

stratum_protocol_t stratum_protocol_from_string(const char *value);
const char *stratum_protocol_to_string(stratum_protocol_t protocol);

#endif /* STRATUM_PROTOCOL_H_ */
