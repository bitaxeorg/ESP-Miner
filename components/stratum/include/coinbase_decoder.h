#ifndef COINBASE_DECODER_H
#define COINBASE_DECODER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "miner_job.h"

#define MAX_ADDRESS_STRING_LEN 128
#define MAX_COINBASE_TX_OUTPUTS 6
#define MAX_SCRIPTPUBKEY_LEN 34
#define MAX_USER_ADDRESSES 4

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
 * @brief Convert user address string (address, address.worker, or hex script)
 *        into canonical binary scriptPubKey bytes.
 * 
 * @param user User string from pool config (e.g. "bc1q...axe1", "1A1z..._w1", "0014...")
 * @param script_out Buffer to store binary scriptPubKey (min MAX_SCRIPTPUBKEY_LEN bytes)
 * @param max_out Maximum capacity of script_out
 * @return size_t Length of parsed scriptPubKey in bytes, or 0 if user is not a valid address/script
 */
size_t coinbase_address_to_scriptpubkey(const char *user, uint8_t *script_out, size_t max_out);

/**
 * @brief Parse user configuration string (which may contain up to MAX_USER_ADDRESSES
 *        comma- or semicolon-separated addresses, each with optional worker suffix)
 *        into canonical binary scriptPubKeys.
 *
 * @param user User string from pool config (e.g. "addr1.w1,addr2.w2" or "addr1")
 * @param scripts_out 2D array [MAX_USER_ADDRESSES][MAX_SCRIPTPUBKEY_LEN]
 * @param script_lens Array to store lengths of parsed scripts [MAX_USER_ADDRESSES]
 * @param max_scripts Maximum number of scripts to parse (up to MAX_USER_ADDRESSES)
 * @return int Number of successfully parsed scripts (0 to max_scripts)
 */
int coinbase_parse_user_scriptpubkeys(const char *user,
                                      uint8_t scripts_out[][MAX_SCRIPTPUBKEY_LEN],
                                      size_t script_lens[],
                                      int max_scripts);

/**
 * @brief Decode Bitcoin varint from binary data
 * 
 * @param data Binary data containing the varint
 * @param data_len Total length of data buffer
 * @param offset Pointer to current offset, will be updated after reading
 * @return Decoded varint value
 */
uint64_t coinbase_decode_varint(const uint8_t *data, size_t data_len, int *offset);

/**
 * @brief Decode Bitcoin address from scriptPubKey
 * 
 * Supports P2PKH, P2SH, P2WPKH, P2WSH, and P2TR address types.
 * Detects network from user_address prefix (bc1/tb1/bcrt1/1/3/m/n/2).
 * 
 * @param script ScriptPubKey binary data
 * @param script_len Length of scriptPubKey
 * @param output Output buffer for address string
 * @param output_len Size of output buffer (should be at least MAX_ADDRESS_STRING_LEN)
 * @param bech32_hrp Bech32 human-readable part ("bc" for mainnet, "tb" for testnet, "bcrt" for regtest)
 * @param is_testnet true for testnet/regtest (affects base58 version bytes)
 */
void coinbase_decode_address_from_scriptpubkey(const uint8_t *script, size_t script_len, 
                                                char *output, size_t output_len,
                                                const char *bech32_hrp, bool is_testnet);

/**
 * @brief Structure representing a decoded coinbase transaction output
 */
typedef struct {
    uint64_t value_satoshis;
    char address[MAX_ADDRESS_STRING_LEN];
    bool is_user_output;
} coinbase_output_t;

/**
 * @brief Result structure for full mining notification processing
 */
typedef struct {
    uint32_t block_height;
    char *scriptsig; // Allocated, must be freed by caller
    coinbase_output_t outputs[MAX_COINBASE_TX_OUTPUTS];
    int output_count;
    uint64_t total_value_satoshis;
    uint64_t user_value_satoshis;
    bool has_user_address;
    bool decode_coinbase_tx;
    bool bip54_signaling;  // BIP-54: nLockTime = height - 1 && nSequence != 0xffffffff
    bool bip110_signaling; // BIP-110: signaling via version bit 4 (0x00000010)
} mining_notification_result_t;

/**
 * @brief Process a miner job (V1 or V2) to extract coinbase and block data
 * 
 * @param job Pointer to the polymorphic miner_job_t
 * @param user_address Payout address of the user
 * @param decode_coinbase_tx Enable coinbase tx decoding
 * @param result Pointer to store the results
 * @return esp_err_t
 */
esp_err_t coinbase_process_miner_job(const miner_job_t *job,
                                     const char *user_address,
                                     bool decode_coinbase_tx,
                                     mining_notification_result_t *result);

/**
 * @brief Invalidate the cached parsed user scriptPubKeys and network detection
 */
void coinbase_clear_user_cache(void);

#endif // COINBASE_DECODER_H
