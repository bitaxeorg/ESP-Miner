#ifndef STRATUM_API_H
#define STRATUM_API_H

#include "cJSON.h"

typedef enum {
    STRATUM_UNKNOWN,
    MINING_NOTIFY,
    MINING_SET_DIFFICULTY
} stratum_method;

typedef struct {
    int id;
    stratum_method method;
    char * method_str;
} stratum_message;

stratum_message parse_stratum_message(const char * stratum_json);

#endif // STRATUM_API_H