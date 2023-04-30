#ifndef STRATUM_API_H
#define STRATUM_API_H

#include "cJSON.h"
#include <stdint.h>

#define MAX_MERKLE_BRANCHES 32
#define PREV_BLOCK_HASH_SIZE 32
#define COINBASE_SIZE 100
#define COINBASE2_SIZE 128

typedef struct {
    uint32_t job_id;
    uint8_t prev_block_hash[PREV_BLOCK_HASH_SIZE];
    uint8_t coinbase_1[COINBASE_SIZE];
    size_t coinbase_1_len;
    uint8_t coinbase_2[COINBASE2_SIZE];
    size_t coinbase_2_len;
    uint8_t merkle_branches[MAX_MERKLE_BRANCHES][PREV_BLOCK_HASH_SIZE];
    size_t n_merkle_branches;
    uint32_t version;
    uint32_t curtime;
    uint32_t bits;
    uint32_t target;
    uint32_t nonce;
} work;

typedef enum {
    STRATUM_UNKNOWN,
    MINING_NOTIFY,
    MINING_SET_DIFFICULTY
} stratum_method;

typedef struct {
    int id;
    stratum_method method;
    char * method_str;
    union {
        work notify_work;
        uint32_t notify_difficulty;
    };
} stratum_message;

void initialize_stratum_buffer();

char * receive_jsonrpc_line(int sockfd);

int subscribe_to_stratum(int socket);

stratum_message parse_stratum_notify_message(const char * stratum_json);

int auth_to_stratum(int socket, const char * username);

#endif // STRATUM_API_H