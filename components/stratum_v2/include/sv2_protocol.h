#ifndef SV2_PROTOCOL_H
#define SV2_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "miner_job.h"

// Frame header size (extension_type[2] + msg_type[1] + msg_length[3])
#define SV2_FRAME_HEADER_SIZE 6

// Common message types
#define SV2_MSG_SETUP_CONNECTION                        0x00
#define SV2_MSG_SETUP_CONNECTION_SUCCESS                0x01
#define SV2_MSG_SETUP_CONNECTION_ERROR                  0x02

// Mining protocol message types (values from SV2 spec / SRI reference)
#define SV2_MSG_OPEN_STANDARD_MINING_CHANNEL            0x10
#define SV2_MSG_OPEN_STANDARD_MINING_CHANNEL_SUCCESS    0x11
#define SV2_MSG_OPEN_MINING_CHANNEL_ERROR               0x12
#define SV2_MSG_OPEN_EXTENDED_MINING_CHANNEL            0x13
#define SV2_MSG_OPEN_EXTENDED_MINING_CHANNEL_SUCCESS    0x14
#define SV2_MSG_NEW_MINING_JOB                          0x15
#define SV2_MSG_NEW_EXTENDED_MINING_JOB                 0x1f
#define SV2_MSG_SUBMIT_SHARES_STANDARD                  0x1a
#define SV2_MSG_SUBMIT_SHARES_EXTENDED                  0x1b
#define SV2_MSG_SUBMIT_SHARES_SUCCESS                   0x1c
#define SV2_MSG_SUBMIT_SHARES_ERROR                     0x1d
#define SV2_MSG_SET_NEW_PREV_HASH                       0x20
#define SV2_MSG_SET_TARGET                              0x21

#define SV2_MAX_MERKLE_BRANCHES 20

// Submit shares frame sizing (6-byte header + 24-byte payload + optional 1-byte len + 32-byte extranonce)
#define SV2_SUBMIT_SHARES_PAYLOAD_SIZE 24
#define SV2_SUBMIT_SHARES_MAX_FRAME_SIZE (SV2_FRAME_HEADER_SIZE + SV2_SUBMIT_SHARES_PAYLOAD_SIZE + 1 + 32)

// Extension type flag for channel messages
#define SV2_CHANNEL_MSG_FLAG 0x8000

// Channel type selection
typedef enum {
    SV2_CHANNEL_UNKNOWN = 0,
    SV2_CHANNEL_STANDARD = 1,
    SV2_CHANNEL_EXTENDED = 2,
} sv2_channel_type_t;

sv2_channel_type_t sv2_channel_type_from_string(const char *s);
const char *sv2_channel_type_to_string(sv2_channel_type_t t);

// Frame header (parsed)
typedef struct {
    uint16_t extension_type;
    uint8_t msg_type;
    uint32_t msg_length; // 24-bit value stored in 32-bit
} sv2_frame_header_t;

#define SV2_PENDING_JOBS_SIZE 8
_Static_assert(SV2_PENDING_JOBS_SIZE <= 16, "SV2_PENDING_JOBS_SIZE must fit in uint16_t bitmask");

#define SV2_MAX_ACTIVE_JOB_IDS 16

struct sv2_noise_ctx;

// SV2 connection state
typedef struct sv2_conn {
    struct sv2_noise_ctx *noise_ctx;
    uint32_t channel_id;
    uint32_t version_mask;
    uint32_t sequence_number;       // also the count of shares submitted
    uint32_t resolved_shares;       // shares the pool has accepted or rejected
    uint8_t target[32]; // U256 LE target
    uint8_t pool_idx;
    bool channel_opened;

    // Pending future candidate jobs ring buffer (both standard and extended channels)
    miner_job_t pending_jobs[SV2_PENDING_JOBS_SIZE];
    uint16_t    pending_jobs_valid; // bitmask of valid slots

    // Latest prev_hash state
    uint8_t prev_hash[32];
    uint32_t prev_hash_ntime;
    uint32_t prev_hash_nbits;
    bool has_prev_hash;

    // Extended channel state (zero for standard channels)
    sv2_channel_type_t channel_type;
    uint8_t  extranonce_prefix[32];
    uint8_t  extranonce_prefix_len;
    uint8_t  extranonce_size;              // total extranonce bytes assigned by pool

    // Active job IDs tracking for duplicate detection
    uint32_t active_job_ids[SV2_MAX_ACTIVE_JOB_IDS];
    int active_job_ids_count;
} sv2_conn_t;

// --- Frame encode/decode ---

// Parse 6-byte frame header. Returns 0 on success.
int sv2_parse_frame_header(const uint8_t *data, sv2_frame_header_t *header);

// Encode 6-byte frame header into dest. Returns 6.
int sv2_encode_frame_header(uint8_t *dest, uint16_t extension_type, uint8_t msg_type, uint32_t msg_length);

// --- Message builders (return total frame size, or -1 on error) ---

int sv2_build_setup_connection(uint8_t *buf, size_t buf_len,
                               const char *host, uint16_t port,
                               const char *vendor, const char *hw_version,
                               const char *firmware, const char *device_id,
                               uint32_t flags);

int sv2_build_open_standard_mining_channel(uint8_t *buf, size_t buf_len,
                                           uint32_t request_id,
                                           const char *user_identity,
                                           float nominal_hash_rate);

int sv2_build_submit_shares(uint8_t *buf, size_t buf_len,
                            uint32_t channel_id, uint32_t sequence_number,
                            uint32_t job_id, uint32_t nonce, uint32_t ntime,
                            uint32_t version, const uint8_t *extranonce,
                            uint8_t extranonce_len);

// --- Message parsers (return 0 on success, -1 on error) ---

int sv2_parse_setup_connection_success(const uint8_t *payload, uint32_t len,
                                       uint16_t *used_version, uint32_t *flags);

int sv2_parse_open_channel_success(const uint8_t *payload, uint32_t len,
                                   uint32_t *request_id, uint32_t *channel_id,
                                   uint8_t target[32],
                                   uint8_t *extranonce_prefix, uint8_t *extranonce_prefix_len,
                                   uint32_t *group_channel_id);

int sv2_parse_new_mining_job(const uint8_t *payload, uint32_t len,
                             uint32_t *channel_id, uint32_t *job_id,
                             bool *has_min_ntime, uint32_t *min_ntime,
                             uint32_t *version,
                             uint8_t merkle_root[32]);

int sv2_parse_set_new_prev_hash(const uint8_t *payload, uint32_t len,
                                uint32_t *channel_id, uint32_t *job_id,
                                uint8_t prev_hash[32],
                                uint32_t *min_ntime, uint32_t *nbits);

int sv2_parse_set_target(const uint8_t *payload, uint32_t len,
                         uint32_t *channel_id, uint8_t max_target[32]);

int sv2_parse_submit_shares_success(const uint8_t *payload, uint32_t len,
                                    uint32_t *channel_id, uint32_t *last_sequence_number,
                                    uint32_t *accepted_count);

int sv2_parse_submit_shares_error(const uint8_t *payload, uint32_t len,
                                  uint32_t *channel_id, uint32_t *seq_num,
                                  char *error_code, size_t error_code_size);

// --- Extended channel message builders/parsers ---

int sv2_build_open_extended_mining_channel(uint8_t *buf, size_t buf_len,
                                           uint32_t request_id, const char *user_identity,
                                           float nominal_hash_rate, uint16_t min_extranonce_size);

int sv2_parse_open_extended_channel_success(const uint8_t *payload, uint32_t len,
                                            uint32_t *request_id, uint32_t *channel_id,
                                            uint8_t target[32], uint16_t *extranonce_size,
                                            uint8_t *extranonce_prefix,
                                            uint8_t *extranonce_prefix_len,
                                            uint32_t *group_channel_id);

int sv2_parse_new_extended_mining_job(const uint8_t *payload, uint32_t len,
                                      uint32_t *channel_id_out, miner_job_t *job_out,
                                      bool *has_min_ntime_out);

#endif /* SV2_PROTOCOL_H */
