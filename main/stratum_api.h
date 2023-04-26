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

void initialize_stratum_buffer();

char * receive_jsonrpc_line(int sockfd);

int subscribe_to_stratum(int socket);

stratum_message parse_stratum_notify_message(const char * stratum_json);

int auth_to_stratum(int socket, const char * username, const char * passwork);

#endif // STRATUM_API_H