#ifndef COINBASE_DECODER_H
#define COINBASE_DECODER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "stratum_api.h"

#define MAX_ADDRESS_STRING_LEN 128
#define MAX_COINBASE_TX_OUTPUTS 6

// Bitcoin Script Opcodes
#define OP_0            0x00
#define OP_PUSHDATA_20  0x14  // Push next 20 bytes
#define OP_PUSHDATA_32  0x20  // Push next 32 bytes
#define OP_1            0x51
#define OP_RETURN       0x6a
#define OP_DUP          0x76
#define OP_EQUAL        0x87
#define OP_EQUALVERIFY  0x88
#define OP_HASH160      0xa9
#define OP_CHECKSIG     0xac

/**
 * @brief Decode Bitcoin varint from binary data
 * 
 * @param data Binary data containing the varint
 * @param offset Pointer to current offset, will be updated after reading
 * @return Decoded varint value
 */
uint64_t coinbase_decode_varint(const uint8_t *data, int *offset);

/**
 * @brief Decode Bitcoin address from scriptPubKey
 * 
 * Supports P2PKH, P2SH, P2WPKH, P2WSH, and P2TR address types.
 * Output format: "<TYPE>:<HEX_HASH>"
 * 
 * @param script ScriptPubKey binary data
 * @param script_len Length of scriptPubKey
 * @param output Output buffer for address string
 * @param output_len Size of output buffer (should be at least MAX_ADDRESS_STRING_LEN)
 */
void coinbase_decode_address_from_scriptpubkey(const uint8_t *script, size_t script_len, 
                                                char *output, size_t output_len);

/**
 * @brief Structure representing a decoded coinbase transaction output
 */
typedef struct {
    uint64_t value_satoshis;
    char address[MAX_ADDRESS_STRING_LEN];
    bool is_user_output;
} coinbase_output_t;

/**
 * @brief Decode a variable-length integer from binary data
 * 
 * @param data Binary data containing the varint
 * @param offset Pointer to current offset (will be updated)
 * @return Decoded integer value
 */
uint64_t coinbase_decode_varint(const uint8_t *data, int *offset);

/**
 * @brief Decode a Bitcoin address from a scriptPubKey
 * 
 * @param script Binary scriptPubKey data
 * @param script_len Length of scriptPubKey
 * @param output Buffer to store the decoded address string
 * @param output_len Size of output buffer
 */
void coinbase_decode_address_from_scriptpubkey(const uint8_t *script, size_t script_len, 
                                                char *output, size_t output_len);

/**
 * @brief Result structure for full mining notification processing
 */
typedef struct {
    double network_difficulty;
    uint32_t block_height;
    char *scriptsig; // Allocated, must be freed by caller
    coinbase_output_t outputs[MAX_COINBASE_TX_OUTPUTS];
    int output_count;
    uint64_t total_value_satoshis;
    uint64_t user_value_satoshis;
} mining_notification_result_t;

/**
 * @brief Process a mining notification to extract all relevant data
 * 
 * @param notification Pointer to the mining notification
 * @param extranonce1 Hex string of extranonce1
 * @param extranonce2_len Length of extranonce2 in bytes
 * @param user_address Payout address of the user
 * @param result Pointer to store the results
 * @return esp_err_t
 */
esp_err_t coinbase_process_notification(const mining_notify *notification,
                                 const char *extranonce1,
                                 int extranonce2_len,
                                 const char *user_address,                                 
                                 mining_notification_result_t *result);

#endif // COINBASE_DECODER_H
