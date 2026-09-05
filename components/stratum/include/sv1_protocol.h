#ifndef SV1_PROTOCOL_H_
#define SV1_PROTOCOL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "miner_job.h"

#define MAX_EXTRANONCE_2_LEN 32
#define MAX_POOL_MESSAGE_LEN 256

typedef enum
{
    METHOD_UNKNOWN,
    MINING_NOTIFY,
    MINING_SET_DIFFICULTY,
    MINING_SET_VERSION_MASK,
    MINING_SET_EXTRANONCE,
    MINING_PING,
    STRATUM_RESULT,
    STRATUM_RESULT_SUBSCRIBE,
    STRATUM_RESULT_CONFIGURE,
    CLIENT_RECONNECT,
    CLIENT_SHOW_MESSAGE,
    CLIENT_GET_VERSION,
} stratum_method;

typedef struct
{
    char *extranonce_str;
    int extranonce_2_len;

    int message_id;
    stratum_method method;

    miner_job_t *job;
    double new_difficulty;
    uint32_t version_mask;

    bool response_success;
    char *error_str;
    char *show_message;
    char *version_string;
} StratumApiV1Message;

/*
 * Encoders return the number of wire bytes, excluding the trailing NUL, or -1
 * on invalid input or insufficient capacity. Successful messages end in '\n'.
 * When buffer is non-NULL and capacity is nonzero, failures leave it empty.
 */
int STRATUM_V1_encode_subscribe(char *buffer, size_t capacity, int message_id,
                                const char *model, const char *version);
int STRATUM_V1_encode_suggest_difficulty(char *buffer, size_t capacity, int message_id,
                                         uint32_t difficulty);
int STRATUM_V1_encode_extranonce_subscribe(char *buffer, size_t capacity, int message_id);
int STRATUM_V1_encode_authorize(char *buffer, size_t capacity, int message_id,
                                const char *username, const char *password);
int STRATUM_V1_encode_pong(char *buffer, size_t capacity, int message_id);
int STRATUM_V1_encode_version_response(char *buffer, size_t capacity, int message_id,
                                       const char *version);
int STRATUM_V1_encode_submit_share(char *buffer, size_t capacity, int message_id,
                                   const char *username, const char *job_id,
                                   const char *extranonce_2, uint32_t ntime,
                                   uint32_t nonce, uint32_t version_bits);
int STRATUM_V1_encode_configure_version_rolling(char *buffer, size_t capacity, int message_id);

/*
 * Zero-initialize a message before its first parse. Each parse resets the prior
 * result. The message owns its returned strings until the next parse or an
 * explicit reset. A mining.notify result requires and is written to the
 * caller-owned job, whose coinbase buffers must already be allocated.
 */
bool STRATUM_V1_parse(StratumApiV1Message *message, const char *stratum_json,
                      miner_job_t *job);
void STRATUM_V1_reset_message(StratumApiV1Message *message);

#endif /* SV1_PROTOCOL_H_ */
