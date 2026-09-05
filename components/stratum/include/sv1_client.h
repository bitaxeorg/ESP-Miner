#ifndef SV1_CLIENT_H_
#define SV1_CLIENT_H_

#include <stdint.h>

#include <esp_transport.h>

#include "miner_job.h"

#define MAX_REQUEST_IDS 1024

typedef enum
{
    DISABLED = 0,
    BUNDLED_CRT = 1,
    CUSTOM_CRT = 2,
} tls_mode;

#define SV1_MAX_ACTIVE_JOB_IDS 16

typedef struct sv1_conn {
    int send_uid;
    char user[256];
    double pool_difficulty;
    uint32_t version_mask;
    uint8_t extranonce1[32];
    uint8_t extranonce1_len;
    uint8_t extranonce2_len;
    char active_job_ids[SV1_MAX_ACTIVE_JOB_IDS][MAX_JOB_ID_LEN];
    int active_job_ids_count;
} sv1_conn_t;

esp_transport_handle_t STRATUM_V1_transport_init(tls_mode tls, const char *cert);

void STRATUM_V1_initialize_buffer(void);

char *STRATUM_V1_receive_jsonrpc_line(esp_transport_handle_t transport);

int STRATUM_V1_subscribe(esp_transport_handle_t transport, int send_uid, const char *model);

int STRATUM_V1_authorize(esp_transport_handle_t transport, int send_uid, const char *username, const char *pass);

int STRATUM_V1_configure_version_rolling(esp_transport_handle_t transport, int send_uid);

int STRATUM_V1_pong(esp_transport_handle_t transport, int message_id);

int STRATUM_V1_send_version(esp_transport_handle_t transport, int message_id);

int STRATUM_V1_suggest_difficulty(esp_transport_handle_t transport, int send_uid, uint32_t difficulty);

int STRATUM_V1_extranonce_subscribe(esp_transport_handle_t transport, int send_uid);

int STRATUM_V1_submit_share(esp_transport_handle_t transport, int send_uid, const char *username, const char *job_id,
                            const char *extranonce_2, const uint32_t ntime, const uint32_t nonce,
                            const uint32_t version_bits, uint64_t *out_sent_time_us);

float STRATUM_V1_get_response_time_ms(int request_id, int64_t receive_time_us);

#endif /* SV1_CLIENT_H_ */
