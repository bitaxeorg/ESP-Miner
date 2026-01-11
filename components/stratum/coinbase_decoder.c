#include "coinbase_decoder.h"
#include "utils.h"
#include "segwit_addr.h"
#include "libbase58.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "mbedtls/sha256.h"

// Wrapper for SHA256 to match libbase58's expected signature
static bool my_sha256(void *digest, const void *data, size_t datasz) {
    mbedtls_sha256(data, datasz, digest, 0);
    return true;
}

static void ensure_base58_init(void) {
    if (b58_sha256_impl == NULL) {
        b58_sha256_impl = my_sha256;
    }
}

uint64_t coinbase_decode_varint(const uint8_t *data, int *offset) {
    uint8_t first_byte = data[*offset];
    (*offset)++;
    
    if (first_byte < 0xFD) {
        return first_byte;
    } else if (first_byte == 0xFD) {
        uint64_t value = data[*offset] | (data[*offset + 1] << 8);
        *offset += 2;
        return value;
    } else if (first_byte == 0xFE) {
        uint64_t value = data[*offset] | (data[*offset + 1] << 8) | 
                        (data[*offset + 2] << 16) | (data[*offset + 3] << 24);
        *offset += 4;
        return value;
    } else { // 0xFF
        uint64_t value = 0;
        for (int i = 0; i < 8; i++) {
            value |= ((uint64_t)data[*offset + i]) << (i * 8);
        }
        *offset += 8;
        return value;
    }
}

void coinbase_decode_address_from_scriptpubkey(const uint8_t *script, size_t script_len, 
                                                char *output, size_t output_len) {
    if (script_len == 0 || output_len < 65) {
        snprintf(output, output_len, "unknown");
        return;
    }
    
    ensure_base58_init();
    
    // P2PKH: OP_DUP OP_HASH160 <20 bytes> OP_EQUALVERIFY OP_CHECKSIG
    if (script_len == 25 && script[0] == OP_DUP && script[1] == OP_HASH160 && 
        script[2] == OP_PUSHDATA_20 && script[23] == OP_EQUALVERIFY && script[24] == OP_CHECKSIG) {
        size_t b58sz = output_len;
        // 0x00 is version for Mainnet P2PKH
        if (b58check_enc(output, &b58sz, 0x00, script + 3, 20)) {
            return;
        }
        // Fallback
        snprintf(output, output_len, "P2PKH:");
        bin2hex(script + 3, 20, output + 6, output_len - 6);
        return;
    }
    
    // P2SH: OP_HASH160 <20 bytes> OP_EQUAL
    if (script_len == 23 && script[0] == OP_HASH160 && script[1] == OP_PUSHDATA_20 && script[22] == OP_EQUAL) {
        size_t b58sz = output_len;
        // 0x05 is version for Mainnet P2SH
        if (b58check_enc(output, &b58sz, 0x05, script + 2, 20)) {
            return;
        }
        // Fallback
        snprintf(output, output_len, "P2SH:");
        bin2hex(script + 2, 20, output + 5, output_len - 5);
        return;
    }
    
    // P2WPKH: OP_0 <20 bytes>
    if (script_len == 22 && script[0] == OP_0 && script[1] == OP_PUSHDATA_20) {
        if (segwit_addr_encode(output, "bc", 0, script + 2, 20)) {
            return;
        }
        // Fallback to hex if encoding fails
        snprintf(output, output_len, "P2WPKH:");
        bin2hex(script + 2, 20, output + 7, output_len - 7);
        return;
    }
    
    // P2WSH: OP_0 <32 bytes>
    if (script_len == 34 && script[0] == OP_0 && script[1] == OP_PUSHDATA_32) {
        if (segwit_addr_encode(output, "bc", 0, script + 2, 32)) {
            return;
        }
        // Fallback to hex if encoding fails
        snprintf(output, output_len, "P2WSH:");
        bin2hex(script + 2, 32, output + 6, output_len - 6);
        return;
    }
    
    // P2TR: OP_1 <32 bytes>
    if (script_len == 34 && script[0] == OP_1 && script[1] == OP_PUSHDATA_32) {
        if (segwit_addr_encode(output, "bc", 1, script + 2, 32)) {
            return;
        }
        // Fallback to hex if encoding fails
        snprintf(output, output_len, "P2TR:");
        bin2hex(script + 2, 32, output + 5, output_len - 5);
        return;
    }

    // OP_RETURN: OP_RETURN <data>
    if (script_len > 0 && script[0] == OP_RETURN) {
        snprintf(output, output_len, "OP_RETURN: ");
        size_t offset = 1;
        
        // Simple check for small pushdata to skip the length byte
        // If script[1] is the length of the remaining data
        if (script_len > 1 && script[1] > 0 && script[1] <= 0x4b && (size_t)script[1] + 2 == script_len) {
            offset = 2;
        }
        
        size_t out_idx = strlen(output);
        for (size_t i = offset; i < script_len && out_idx < output_len - 1; i++) {
            unsigned char c = script[i];
            output[out_idx++] = isprint(c) ? c : '.';
        }
        output[out_idx] = '\0';
        return;
    }
    
    // Unknown format - just show hex
    snprintf(output, output_len, "UNKNOWN:");
    size_t hex_len = script_len < 32 ? script_len : 32; // Limit to 32 bytes
    bin2hex(script, hex_len, output + 8, output_len - 8);
}

esp_err_t coinbase_process_notification(const mining_notify *notification,
                                 const char *extranonce1,
                                 int extranonce2_len,
                                 const char *user_address,
                                 mining_notification_result_t *result) {
    if (!notification || !extranonce1 || !result) return ESP_ERR_INVALID_ARG;

    // Initialize result
    result->total_value_satoshis = 0;

    // 1. Calculate difficulty
    result->network_difficulty = networkDifficulty(notification->target);

    // 2. Parse Coinbase 1 for ScriptSig info
    int coinbase_1_len = strlen(notification->coinbase_1) / 2;
    int coinbase_1_offset = 41; // Skip version (4), inputcount (1), prevhash (32), vout (4)
    
    if (coinbase_1_len < coinbase_1_offset) return ESP_ERR_INVALID_ARG;

    uint8_t scriptsig_len;
    hex2bin(notification->coinbase_1 + (coinbase_1_offset * 2), &scriptsig_len, 1);
    coinbase_1_offset++;

    if (coinbase_1_len < coinbase_1_offset) return ESP_ERR_INVALID_ARG;
    
    uint8_t block_height_len;
    hex2bin(notification->coinbase_1 + (coinbase_1_offset * 2), &block_height_len, 1);
    coinbase_1_offset++;

    if (coinbase_1_len < coinbase_1_offset || block_height_len == 0 || block_height_len > 4) return ESP_ERR_INVALID_ARG;

    result->block_height = 0;
    hex2bin(notification->coinbase_1 + (coinbase_1_offset * 2), (uint8_t *)&result->block_height, block_height_len);
    coinbase_1_offset += block_height_len;

    // Calculate remaining scriptsig length (excluding block height part)
    int scriptsig_length = scriptsig_len - 1 - block_height_len;
    size_t extranonce1_len = strlen(extranonce1) / 2;
    
    // Check if scriptsig extends into coinbase_2 (meaning it covers the extranonces)
    // If so, subtract extranonce lengths to get just the miner tag length
    if (coinbase_1_len - coinbase_1_offset < scriptsig_length) {
        scriptsig_length -= (extranonce1_len + extranonce2_len);
    }
    
    // Extract miner tag if present
    if (scriptsig_length > 0) {
        char *tag = malloc(scriptsig_length + 1);
        if (tag) {
            int coinbase_1_tag_len = coinbase_1_len - coinbase_1_offset;
            if (coinbase_1_tag_len > scriptsig_length) {
                coinbase_1_tag_len = scriptsig_length;
            }

            hex2bin(notification->coinbase_1 + (coinbase_1_offset * 2), (uint8_t *)tag, coinbase_1_tag_len);

            int coinbase_2_tag_len = scriptsig_length - coinbase_1_tag_len;
            int coinbase_2_len = strlen(notification->coinbase_2) / 2;
            
            if (coinbase_2_len >= coinbase_2_tag_len) {
                if (coinbase_2_tag_len > 0) {
                    hex2bin(notification->coinbase_2, (uint8_t *)tag + coinbase_1_tag_len, coinbase_2_tag_len);
                }
                
                // Filter non-printable characters
                for (int i = 0; i < scriptsig_length; i++) {
                    if (!isprint((unsigned char)tag[i])) {
                        tag[i] = '.';
                    }                }
                tag[scriptsig_length] = '\0';
                result->scriptsig = tag;
            } else {
                free(tag);
                // Tag extraction failed due to length mismatch, but we can continue
            }
        }
    }

    // 3. Parse Coinbase 2 for Outputs
    // Calculate offset in coinbase_2 where outputs start
    // Re-calculate raw remainder length without subtracting extranonces
    int raw_scriptsig_remainder = (scriptsig_len - 1 - block_height_len) - (coinbase_1_len - coinbase_1_offset);
    
    int coinbase_2_offset = 0;
    if (raw_scriptsig_remainder > 0) {
        // Subtract extranonce lengths to see what's left for coinbase_2
        int remainder_in_coinbase_2 = raw_scriptsig_remainder - (extranonce1_len + extranonce2_len);
        if (remainder_in_coinbase_2 > 0) {
            coinbase_2_offset = remainder_in_coinbase_2;
        }
    }
    
    int coinbase_2_len = strlen(notification->coinbase_2) / 2;
    uint8_t *coinbase_2_bin = malloc(coinbase_2_len);
    if (!coinbase_2_bin) {
        return ESP_ERR_NO_MEM; // Memory error is fatal
    }
    
    hex2bin(notification->coinbase_2, coinbase_2_bin, coinbase_2_len);
    
    int offset = coinbase_2_offset;
    
    // Skip sequence (4 bytes)
    if (offset + 4 > coinbase_2_len) {
        free(coinbase_2_bin);
        return ESP_ERR_INVALID_ARG; // No room for outputs, but valid notification processed so far
    }
    offset += 4;
    
    // Decode output count
    if (offset >= coinbase_2_len) {
        free(coinbase_2_bin);
        return ESP_ERR_INVALID_ARG;
    }
    
    uint64_t num_outputs = coinbase_decode_varint(coinbase_2_bin, &offset);
    result->output_count = 0;
    
    // Parse each output
    for (uint64_t i = 0; i < num_outputs && offset < coinbase_2_len; i++) {
        // Read value (8 bytes, little-endian)
        if (offset + 8 > coinbase_2_len) break;

        uint64_t value_satoshis = 0;
        for (int j = 0; j < 8; j++) {
            value_satoshis |= ((uint64_t)coinbase_2_bin[offset + j]) << (j * 8);
        }
        offset += 8;

        // Read scriptPubKey length
        if (offset >= coinbase_2_len) break;
        uint64_t script_len = coinbase_decode_varint(coinbase_2_bin, &offset);

        // Read scriptPubKey
        if (offset + script_len > coinbase_2_len) break;

        if (value_satoshis > 0) {
            char output_address[MAX_ADDRESS_STRING_LEN];
            coinbase_decode_address_from_scriptpubkey(coinbase_2_bin + offset, script_len, output_address, MAX_ADDRESS_STRING_LEN);
            bool is_user_address = strncmp(user_address, output_address, strlen(output_address)) == 0;

            // Add to total value
            result->total_value_satoshis += value_satoshis;
            if (is_user_address) result->user_value_satoshis += value_satoshis;

            if (i < MAX_COINBASE_TX_OUTPUTS) {
                strncpy(result->outputs[i].address, output_address, MAX_ADDRESS_STRING_LEN);
                result->outputs[i].value_satoshis = value_satoshis;
                result->outputs[i].is_user_output = is_user_address;
                result->output_count++;
            }
        } else {
            if (i < MAX_COINBASE_TX_OUTPUTS) {
                coinbase_decode_address_from_scriptpubkey(coinbase_2_bin + offset, script_len, result->outputs[i].address, MAX_ADDRESS_STRING_LEN);
                result->outputs[i].value_satoshis = 0;
                result->outputs[i].is_user_output = false;
                result->output_count++;
            }
        }

        offset += script_len;
    }
    
    free(coinbase_2_bin);
    return ESP_OK;
}
