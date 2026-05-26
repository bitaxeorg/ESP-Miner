#ifndef LOCAL_WORK_BUILDER_H_
#define LOCAL_WORK_BUILDER_H_

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "mining.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOCAL_WORK_ADDRESS_MAX_LEN 128
#define LOCAL_WORK_COMMITMENT_HEX_LEN 96
#define LOCAL_WORK_MAX_SCRIPT_PUBKEY_LEN 83
#define LOCAL_WORK_MAX_MERKLE_BRANCHES 32
#define LOCAL_WORK_DEFAULT_TAG "/Local Work/"

typedef struct {
    uint64_t value_sats;
    char address[LOCAL_WORK_ADDRESS_MAX_LEN];
} local_work_payout_output_t;

typedef struct {
    uint32_t ref_count;
    uint8_t *tx_data;
    uint32_t *tx_offsets;
    uint32_t *tx_lengths;
    uint32_t *tx_weights;
    size_t tx_count;
    size_t tx_data_len;
} local_work_tx_bundle_t;

typedef struct {
    uint32_t version;
    uint32_t nbits;
    uint32_t curtime;
    uint32_t height;
    uint32_t weight_limit;
    uint32_t tx_weight_total;
    uint64_t coinbase_value;
    char previous_block_hash[65];
    char default_witness_commitment[LOCAL_WORK_COMMITMENT_HEX_LEN];
    uint8_t *txid_hashes;
    size_t tx_count;
    local_work_tx_bundle_t *tx_bundle;
} local_work_template_t;

typedef struct {
    bm_job bm_job;
    uint8_t *coinbase_tx;
    size_t coinbase_tx_len;
    uint8_t *coinbase_legacy_tx;
    size_t coinbase_legacy_tx_len;
    uint8_t coinbase_txid[32];
    uint8_t merkle_root[32];
    uint8_t *merkle_path;
    size_t merkle_path_count;
    uint64_t slot0_value_sats;
    uint32_t version;
    uint32_t nbits;
    uint32_t ntime;
    uint32_t height;
    char previous_block_hash[65];
} local_work_job_t;

esp_err_t local_work_address_to_script(const char *address,
                                            uint8_t *script,
                                            size_t script_cap,
                                            size_t *script_len);

esp_err_t local_work_parse_template_json(const char *json,
                                              local_work_template_t *template_out);

esp_err_t local_work_parse_template_json_with_stage(const char *json,
                                                         local_work_template_t *template_out,
                                                         char *stage,
                                                         size_t stage_len);

esp_err_t local_work_parse_template_json_buffer(char *json,
                                                     local_work_template_t *template_out,
                                                     char *stage,
                                                     size_t stage_len);

void local_work_template_free(local_work_template_t *template_data);

void local_work_tx_bundle_retain(local_work_tx_bundle_t *bundle);

void local_work_tx_bundle_release(local_work_tx_bundle_t *bundle);

esp_err_t local_work_parse_payout_outputs_json(const char *json,
                                                    local_work_payout_output_t *outputs,
                                                    size_t max_outputs,
                                                    size_t *output_count,
                                                    uint64_t *sequence);

esp_err_t local_work_build_coinbase(const local_work_template_t *template_data,
                                         const char *slot0_address,
                                         const local_work_payout_output_t *local_work_outputs,
                                         size_t local_work_output_count,
                                         const uint8_t *extranonce,
                                         size_t extranonce_len,
                                         const char *tag,
                                         uint8_t **coinbase_tx,
                                         size_t *coinbase_tx_len,
                                         uint8_t coinbase_txid[32],
                                         uint64_t *slot0_value_sats);

esp_err_t local_work_estimate_coinbase_weight(const local_work_template_t *template_data,
                                                   const char *slot0_address,
                                                   const local_work_payout_output_t *local_work_outputs,
                                                   size_t local_work_output_count,
                                                   size_t extranonce_len,
                                                   const char *tag,
                                                   uint32_t *coinbase_weight,
                                                   size_t *coinbase_full_len);

esp_err_t local_work_trim_template_to_weight(local_work_template_t *template_data,
                                                  uint32_t coinbase_weight);

size_t local_work_varint_len(uint64_t value);

esp_err_t local_work_compute_merkle_path(const uint8_t coinbase_txid[32],
                                              const uint8_t *txid_hashes,
                                              size_t tx_count,
                                              uint8_t **merkle_path,
                                              size_t *merkle_path_count,
                                              uint8_t merkle_root[32]);

esp_err_t local_work_compute_root_from_path(const uint8_t coinbase_txid[32],
                                                 const uint8_t *merkle_path,
                                                 size_t merkle_path_count,
                                                 uint8_t merkle_root[32]);

esp_err_t local_work_build_job(const local_work_template_t *template_data,
                                    const char *slot0_address,
                                    const local_work_payout_output_t *local_work_outputs,
                                    size_t local_work_output_count,
                                    const uint8_t *extranonce,
                                    size_t extranonce_len,
                                    const char *tag,
                                    double submit_difficulty,
                                    local_work_job_t *job_out);

esp_err_t local_work_build_job_from_path(const local_work_template_t *template_data,
                                              const char *slot0_address,
                                              const local_work_payout_output_t *local_work_outputs,
                                              size_t local_work_output_count,
                                              const uint8_t *merkle_path,
                                              size_t merkle_path_count,
                                              const uint8_t *extranonce,
                                              size_t extranonce_len,
                                              const char *tag,
                                              double submit_difficulty,
                                              local_work_job_t *job_out);

void local_work_job_free(local_work_job_t *job);

esp_err_t local_work_serialize_header(const local_work_job_t *job,
                                           uint32_t nonce,
                                           uint32_t rolled_version,
                                           uint8_t header[80]);

esp_err_t local_work_hash_to_display_hex(const uint8_t hash[32],
                                              char *hex,
                                              size_t hex_len);

#ifdef __cplusplus
}
#endif

#endif // LOCAL_WORK_BUILDER_H_
