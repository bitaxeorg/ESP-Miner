#include "coinbase_decoder.h"
#include "utils.h"
#include "segwit_addr.h"
#include "libbase58.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>

#define BIP110_SIGNAL_BIT 4
#define BIP110_SIGNAL_EXPIRY_BLOCK 965664

// Wrapper for SHA256 to match libbase58's expected signature
static bool my_sha256(void *digest, const void *data, size_t datasz) {
    sha256_bin(data, datasz, digest);
    return true;
}

static void ensure_base58_init(void) {
    if (b58_sha256_impl == NULL) {
        b58_sha256_impl = my_sha256;
    }
}

uint64_t coinbase_decode_varint(const uint8_t *data, size_t data_len, int *offset) {
    if (!data || !offset || (size_t)*offset >= data_len) {
        return 0;
    }
    uint8_t first_byte = data[*offset];
    (*offset)++;
    
    if (first_byte < 0xFD) {
        return first_byte;
    } else if (first_byte == 0xFD) {
        if ((size_t)*offset + 2 > data_len) {
            *offset = data_len;
            return 0;
        }
        uint64_t value = (uint64_t)data[*offset] | ((uint64_t)data[*offset + 1] << 8);
        *offset += 2;
        return value;
    } else if (first_byte == 0xFE) {
        if ((size_t)*offset + 4 > data_len) {
            *offset = data_len;
            return 0;
        }
        uint64_t value = (uint64_t)data[*offset] | ((uint64_t)data[*offset + 1] << 8) | 
                         ((uint64_t)data[*offset + 2] << 16) | ((uint64_t)data[*offset + 3] << 24);
        *offset += 4;
        return value;
    } else { // 0xFF
        if ((size_t)*offset + 8 > data_len) {
            *offset = data_len;
            return 0;
        }
        uint64_t value = 0;
        for (int i = 0; i < 8; i++) {
            value |= ((uint64_t)data[*offset + i]) << (i * 8);
        }
        *offset += 8;
        return value;
    }
}

void coinbase_decode_address_from_scriptpubkey(const uint8_t *script, size_t script_len, 
                                                char *output, size_t output_len,
                                                const char *bech32_hrp, bool is_testnet) {
    if (script_len == 0 || output_len < 65) {
        snprintf(output, output_len, "unknown");
        return;
    }
    
    ensure_base58_init();
    
    uint8_t p2pkh_version = is_testnet ? 0x6F : 0x00;
    uint8_t p2sh_version  = is_testnet ? 0xC4 : 0x05;

    // P2PKH: OP_DUP OP_HASH160 <20 bytes> OP_EQUALVERIFY OP_CHECKSIG
    if (script_len == 25 && script[0] == OP_DUP && script[1] == OP_HASH160 && 
        script[2] == OP_PUSHDATA_20 && script[23] == OP_EQUALVERIFY && script[24] == OP_CHECKSIG) {
        size_t b58sz = output_len;
        if (b58check_enc(output, &b58sz, p2pkh_version, script + 3, 20)) {
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
        if (b58check_enc(output, &b58sz, p2sh_version, script + 2, 20)) {
            return;
        }
        // Fallback
        snprintf(output, output_len, "P2SH:");
        bin2hex(script + 2, 20, output + 5, output_len - 5);
        return;
    }
    
    // P2WPKH: OP_0 <20 bytes>
    if (script_len == 22 && script[0] == OP_0 && script[1] == OP_PUSHDATA_20) {
        if (segwit_addr_encode(output, bech32_hrp, 0, script + 2, 20)) {
            return;
        }
        // Fallback to hex if encoding fails
        snprintf(output, output_len, "P2WPKH:");
        bin2hex(script + 2, 20, output + 7, output_len - 7);
        return;
    }
    
    // P2WSH: OP_0 <32 bytes>
    if (script_len == 34 && script[0] == OP_0 && script[1] == OP_PUSHDATA_32) {
        if (segwit_addr_encode(output, bech32_hrp, 0, script + 2, 32)) {
            return;
        }
        // Fallback to hex if encoding fails
        snprintf(output, output_len, "P2WSH:");
        bin2hex(script + 2, 32, output + 6, output_len - 6);
        return;
    }
    
    // P2TR: OP_1 <32 bytes>
    if (script_len == 34 && script[0] == OP_1 && script[1] == OP_PUSHDATA_32) {
        if (segwit_addr_encode(output, bech32_hrp, 1, script + 2, 32)) {
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

static const char *coinbase_detect_bech32_hrp(const char *addr) {
    if (!addr) return NULL;
    while (*addr && isspace((unsigned char)*addr)) addr++;
    if (strncasecmp(addr, "bcrt1", 5) == 0) return "bcrt";
    if (strncasecmp(addr, "tb1", 3) == 0)   return "tb";
    if (strncasecmp(addr, "bc1", 3) == 0)   return "bc";
    return NULL;
}

static void coinbase_detect_network(const char *addr, const char **bech32_hrp, bool *is_testnet) {
    const char *hrp = coinbase_detect_bech32_hrp(addr);
    if (hrp) {
        if (bech32_hrp) *bech32_hrp = hrp;
        if (is_testnet) *is_testnet = (strcmp(hrp, "bc") != 0);
        return;
    }
    if (addr) {
        while (*addr && isspace((unsigned char)*addr)) addr++;
        if (*addr == 'm' || *addr == 'n' || *addr == '2' || *addr == 'M' || *addr == 'N') {
            if (bech32_hrp) *bech32_hrp = "tb";
            if (is_testnet) *is_testnet = true;
            return;
        }
    }
    if (bech32_hrp) *bech32_hrp = "bc";
    if (is_testnet) *is_testnet = false;
}

size_t coinbase_address_to_scriptpubkey(const char *user, uint8_t *script_out, size_t max_out) {
    if (!user || !script_out || max_out < MAX_SCRIPTPUBKEY_LEN) {
        return 0;
    }

    // Trim leading whitespace
    while (isspace((unsigned char)*user)) {
        user++;
    }
    if (*user == '\0') {
        return 0;
    }

    // Copy to candidate buffer
    char candidate[MAX_ADDRESS_STRING_LEN];
    strncpy(candidate, user, sizeof(candidate) - 1);
    candidate[sizeof(candidate) - 1] = '\0';

    // Strip worker or diff delimiters ('.', '_', '/', '+', ':')
    char *delim = strpbrk(candidate, "._/+:");
    if (delim) {
        *delim = '\0';
    }

    // Trim trailing whitespace
    size_t cand_len = strlen(candidate);
    while (cand_len > 0 && isspace((unsigned char)candidate[cand_len - 1])) {
        candidate[--cand_len] = '\0';
    }
    if (cand_len == 0) {
        return 0;
    }

    // 1. Check Bech32 / Bech32m (P2WPKH, P2WSH, P2TR)
    const char *hrp = coinbase_detect_bech32_hrp(candidate);

    if (hrp != NULL) {
        int witver = 0;
        uint8_t witprog[40];
        size_t witprog_len = 0;
        if (segwit_addr_decode(&witver, witprog, &witprog_len, hrp, candidate)) {
            if (witver == 0) {
                if (witprog_len == 20) {
                    // P2WPKH: OP_0 OP_PUSHDATA_20 <20 bytes> (22 bytes)
                    if (max_out < 22) return 0;
                    script_out[0] = OP_0;
                    script_out[1] = OP_PUSHDATA_20;
                    memcpy(script_out + 2, witprog, 20);
                    return 22;
                } else if (witprog_len == 32) {
                    // P2WSH: OP_0 OP_PUSHDATA_32 <32 bytes> (34 bytes)
                    if (max_out < 34) return 0;
                    script_out[0] = OP_0;
                    script_out[1] = OP_PUSHDATA_32;
                    memcpy(script_out + 2, witprog, 32);
                    return 34;
                }
            } else if (witver == 1) {
                if (witprog_len == 32) {
                    // P2TR: OP_1 OP_PUSHDATA_32 <32 bytes> (34 bytes)
                    if (max_out < 34) return 0;
                    script_out[0] = OP_1;
                    script_out[1] = OP_PUSHDATA_32;
                    memcpy(script_out + 2, witprog, 32);
                    return 34;
                }
            } else if (witver >= 2 && witver <= 16) {
                if (max_out < 2 + witprog_len) return 0;
                script_out[0] = (uint8_t)(0x50 + witver);
                script_out[1] = (uint8_t)witprog_len;
                memcpy(script_out + 2, witprog, witprog_len);
                return 2 + witprog_len;
            }
        }
    }

    // 2. Check Base58Check (P2PKH, P2SH)
    ensure_base58_init();
    uint8_t b58bin[25];
    size_t binsz = sizeof(b58bin);
    if (b58tobin(b58bin, &binsz, candidate, cand_len)) {
        if (binsz == 25 && b58check(b58bin, 25, candidate, cand_len) >= 0) {
            uint8_t ver = b58bin[0];
            if (ver == 0x00 || ver == 0x6F) {
                // P2PKH: OP_DUP OP_HASH160 OP_PUSHDATA_20 <20 bytes> OP_EQUALVERIFY OP_CHECKSIG (25 bytes)
                if (max_out < 25) return 0;
                script_out[0] = OP_DUP;
                script_out[1] = OP_HASH160;
                script_out[2] = OP_PUSHDATA_20;
                memcpy(script_out + 3, b58bin + 1, 20);
                script_out[23] = OP_EQUALVERIFY;
                script_out[24] = OP_CHECKSIG;
                return 25;
            } else if (ver == 0x05 || ver == 0xC4) {
                // P2SH: OP_HASH160 OP_PUSHDATA_20 <20 bytes> OP_EQUAL (23 bytes)
                if (max_out < 23) return 0;
                script_out[0] = OP_HASH160;
                script_out[1] = OP_PUSHDATA_20;
                memcpy(script_out + 2, b58bin + 1, 20);
                script_out[22] = OP_EQUAL;
                return 23;
            }
        }
    }

    // 3. Check Direct Raw Hex scriptPubKey (e.g. 0014..., 5120..., 76a914...)
    if (cand_len >= 44 && cand_len <= 80 && (cand_len % 2 == 0)) {
        bool all_hex = true;
        for (size_t i = 0; i < cand_len; i++) {
            if (!isxdigit((unsigned char)candidate[i])) {
                all_hex = false;
                break;
            }
        }
        if (all_hex) {
            size_t raw_len = cand_len / 2;
            if (max_out >= raw_len) {
                hex2bin(candidate, script_out, raw_len);
                return raw_len;
            }
        }
    }

    return 0;
}

int coinbase_parse_user_scriptpubkeys(const char *user,
                                      uint8_t scripts_out[][MAX_SCRIPTPUBKEY_LEN],
                                      size_t script_lens[],
                                      int max_scripts) {
    if (!user || !scripts_out || !script_lens || max_scripts <= 0) {
        return 0;
    }

    int count = 0;
    const char *start = user;

    while (*start && count < max_scripts) {
        // Skip leading delimiters or whitespace
        while (*start && (*start == ',' || *start == ';' || isspace((unsigned char)*start))) {
            start++;
        }
        if (!*start) break;

        // Find delimiter separating addresses (comma or semicolon)
        const char *end = start;
        while (*end && *end != ',' && *end != ';') {
            end++;
        }

        size_t token_len = end - start;
        if (token_len > 0 && token_len < MAX_ADDRESS_STRING_LEN) {
            char token[MAX_ADDRESS_STRING_LEN];
            memcpy(token, start, token_len);
            token[token_len] = '\0';

            size_t slen = coinbase_address_to_scriptpubkey(token,
                                                           scripts_out[count],
                                                           MAX_SCRIPTPUBKEY_LEN);
            if (slen > 0) {
                script_lens[count] = slen;
                count++;
            }
        }

        start = end;
    }

    return count;
}

typedef struct {
    uint64_t value_satoshis;
    const uint8_t *script_ptr;
    size_t script_len;
    bool is_user_output;
} display_output_ref_t;

static esp_err_t parse_coinbase_suffix(const miner_job_t *job,
                                       int offset,
                                       const uint8_t user_scripts[MAX_USER_ADDRESSES][MAX_SCRIPTPUBKEY_LEN],
                                       const size_t user_script_lens[MAX_USER_ADDRESSES],
                                       int user_script_count,
                                       const char *bech32_hrp,
                                       bool is_testnet,
                                       bool decode_coinbase_tx,
                                       mining_notification_result_t *result)
{
    int coinbase_2_len = job->coinbase_suffix_len;
    const uint8_t *coinbase_2_bin = job->coinbase_suffix;

    // Read sequence (4 bytes) for BIP-54 detection
    if (offset + 4 > coinbase_2_len) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t nSequence = 0;
    for (int i = 0; i < 4; i++) {
        nSequence |= ((uint32_t)coinbase_2_bin[offset + i]) << (i * 8);
    }
    offset += 4;

    // Decode output count
    if (offset >= coinbase_2_len) {
        return ESP_ERR_INVALID_ARG;
    }

    uint64_t num_outputs = coinbase_decode_varint(coinbase_2_bin, coinbase_2_len, &offset);
    if (num_outputs == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    result->output_count = 0;

    display_output_ref_t display_refs[MAX_COINBASE_TX_OUTPUTS];
    int display_count = 0;
    bool user_output_displayed = false;

    // Parse each output
    for (uint64_t i = 0; i < num_outputs; i++) {
        // Read value (8 bytes, little-endian)
        if (offset > coinbase_2_len || coinbase_2_len - offset < 8) {
            return ESP_ERR_INVALID_ARG;
        }

        uint64_t value_satoshis = 0;
        for (int j = 0; j < 8; j++) {
            value_satoshis |= ((uint64_t)coinbase_2_bin[offset + j]) << (j * 8);
        }
        offset += 8;

        // Add to total value with overflow protection
        if (UINT64_MAX - result->total_value_satoshis < value_satoshis) {
            return ESP_ERR_INVALID_ARG;
        }
        result->total_value_satoshis += value_satoshis;

        // Read scriptPubKey length
        if (offset >= coinbase_2_len) {
            return ESP_ERR_INVALID_ARG;
        }
        uint64_t script_len = coinbase_decode_varint(coinbase_2_bin, coinbase_2_len, &offset);

        if (offset > coinbase_2_len || script_len > (size_t)(coinbase_2_len - offset)) {
            return ESP_ERR_INVALID_ARG;
        }

        if (decode_coinbase_tx) {
            const uint8_t *script_ptr = coinbase_2_bin + offset;

            // Constant-time binary scriptPubKey matching
            bool is_user_output = false;
            if (user_script_count > 0) {
                for (int u = 0; u < user_script_count; u++) {
                    if (script_len == user_script_lens[u] &&
                        memcmp(script_ptr, user_scripts[u], script_len) == 0) {
                        is_user_output = true;
                        break;
                    }
                }
            }

            if (is_user_output) {
                result->user_value_satoshis += value_satoshis;
            }

            // Track display output references (max 6 slots)
            if (display_count < MAX_COINBASE_TX_OUTPUTS) {
                display_refs[display_count].value_satoshis = value_satoshis;
                display_refs[display_count].script_ptr = script_ptr;
                display_refs[display_count].script_len = (size_t)script_len;
                display_refs[display_count].is_user_output = is_user_output;
                if (is_user_output) {
                    user_output_displayed = true;
                }
                display_count++;
            } else if (is_user_output && !user_output_displayed) {
                // Ocean TIDES Slot Guarantee: user payout found beyond slot 5!
                // Displace slot 5 so user payout is guaranteed to be visible in WebUI/logs.
                display_refs[MAX_COINBASE_TX_OUTPUTS - 1].value_satoshis = value_satoshis;
                display_refs[MAX_COINBASE_TX_OUTPUTS - 1].script_ptr = script_ptr;
                display_refs[MAX_COINBASE_TX_OUTPUTS - 1].script_len = (size_t)script_len;
                display_refs[MAX_COINBASE_TX_OUTPUTS - 1].is_user_output = true;
                user_output_displayed = true;
            }
        }

        offset += script_len;
    }

    // Lazy address string decoding: only decode the displayed outputs (<= 6)
    if (decode_coinbase_tx) {
        result->output_count = display_count;
        for (int k = 0; k < display_count; k++) {
            result->outputs[k].value_satoshis = display_refs[k].value_satoshis;
            result->outputs[k].is_user_output = display_refs[k].is_user_output;
            coinbase_decode_address_from_scriptpubkey(display_refs[k].script_ptr,
                                                      display_refs[k].script_len,
                                                      result->outputs[k].address,
                                                      MAX_ADDRESS_STRING_LEN,
                                                      bech32_hrp,
                                                      is_testnet);
        }
    }

    // Read nLockTime (exact 4 bytes at the end of the transaction)
    if (offset > coinbase_2_len || coinbase_2_len - offset != 4U) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t nLockTime = 0;
    for (int i = 0; i < 4; i++) {
        nLockTime |= ((uint32_t)coinbase_2_bin[offset + i]) << (i * 8);
    }

    // Detect BIP-54 signaling: nLockTime = block_height - 1 AND nSequence != 0xffffffff
    result->bip54_signaling = decode_coinbase_tx && (result->block_height > 0) &&
                              (nLockTime == result->block_height - 1) && (nSequence != 0xffffffff);

    return ESP_OK;
}

static char s_cached_user[128] = "";
static uint8_t s_cached_scripts[MAX_USER_ADDRESSES][MAX_SCRIPTPUBKEY_LEN];
static size_t s_cached_script_lens[MAX_USER_ADDRESSES];
static int s_cached_script_count = 0;
static const char *s_cached_bech32_hrp = "bc";
static bool s_cached_is_testnet = false;
static bool s_cache_valid = false;

void coinbase_clear_user_cache(void) {
    s_cache_valid = false;
    s_cached_user[0] = '\0';
    s_cached_script_count = 0;
    s_cached_bech32_hrp = "bc";
    s_cached_is_testnet = false;
}

esp_err_t coinbase_process_miner_job(const miner_job_t *job,
                                     const char *user_address,
                                     bool decode_coinbase_tx,
                                     mining_notification_result_t *result) {
    if (!job || !result) return ESP_ERR_INVALID_ARG;

    // Initialize result
    result->total_value_satoshis = 0;
    result->user_value_satoshis = 0;
    result->has_user_address = false;
    result->decode_coinbase_tx = decode_coinbase_tx;

    const char *bech32_hrp = "bc";
    bool is_testnet = false;
    int user_script_count = 0;

    if (decode_coinbase_tx && user_address) {
        if (!s_cache_valid || strcmp(user_address, s_cached_user) != 0) {
            strncpy(s_cached_user, user_address, sizeof(s_cached_user) - 1);
            s_cached_user[sizeof(s_cached_user) - 1] = '\0';

            s_cached_script_count = coinbase_parse_user_scriptpubkeys(user_address,
                                                                      s_cached_scripts,
                                                                      s_cached_script_lens,
                                                                      MAX_USER_ADDRESSES);

            coinbase_detect_network(user_address, &s_cached_bech32_hrp, &s_cached_is_testnet);
            s_cache_valid = true;
        }

        user_script_count = s_cached_script_count;
        bech32_hrp = s_cached_bech32_hrp;
        is_testnet = s_cached_is_testnet;
        result->has_user_address = (user_script_count > 0);
    }

    // Parse Coinbase prefix for ScriptSig info
    int coinbase_1_len = job->coinbase_prefix_len;
    int coinbase_1_offset = 41; // Skip version (4), inputcount (1), prevhash (32), vout (4)
    
    if (coinbase_1_len < coinbase_1_offset + 1) return ESP_ERR_INVALID_ARG;

    uint8_t scriptsig_len = job->coinbase_prefix[coinbase_1_offset++];

    if (coinbase_1_len < coinbase_1_offset + 1) return ESP_ERR_INVALID_ARG;
    
    uint8_t block_height_len = job->coinbase_prefix[coinbase_1_offset++];

    if (block_height_len == 0 || block_height_len > 4 || coinbase_1_len < coinbase_1_offset + block_height_len) return ESP_ERR_INVALID_ARG;

    result->block_height = 0;
    memcpy(&result->block_height, job->coinbase_prefix + coinbase_1_offset, block_height_len);
    coinbase_1_offset += block_height_len;

    // Detect BIP-110 signaling: check if bit 4 (0x00000010) is set in version
    result->bip110_signaling = decode_coinbase_tx && result->block_height < BIP110_SIGNAL_EXPIRY_BLOCK && (job->version & (1U << BIP110_SIGNAL_BIT)) != 0;

    // Calculate remaining scriptsig length (excluding block height part)
    int scriptsig_length = scriptsig_len - 1 - block_height_len;
    size_t extranonce1_len = job->extranonce1_len;
    size_t extranonce2_len = job->extranonce2_len;
    
    // Check if scriptsig extends into coinbase_suffix (meaning it covers the extranonces)
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

            if (coinbase_1_tag_len > 0) {
                memcpy(tag, job->coinbase_prefix + coinbase_1_offset, coinbase_1_tag_len);
            }

            int coinbase_2_tag_len = scriptsig_length - coinbase_1_tag_len;
            int coinbase_2_len = job->coinbase_suffix_len;
            
            if (coinbase_2_len >= coinbase_2_tag_len) {
                if (coinbase_2_tag_len > 0) {
                    memcpy(tag + coinbase_1_tag_len, job->coinbase_suffix, coinbase_2_tag_len);
                }
                
                // Filter non-printable characters
                for (int i = 0; i < scriptsig_length; i++) {
                    if (!isprint((unsigned char)tag[i])) {
                        tag[i] = '.';
                    }
                }
                tag[scriptsig_length] = '\0';
                result->scriptsig = tag;
            } else {
                free(tag);
            }
        }
    }

    // 3. Calculate offset in coinbase_2 where scriptSig ends and nSequence/outputs begin
    int raw_scriptsig_remainder = (scriptsig_len - 1 - block_height_len) - (coinbase_1_len - coinbase_1_offset);
    int coinbase_2_offset = 0;
    if (raw_scriptsig_remainder > 0) {
        int remainder_in_coinbase_2 = raw_scriptsig_remainder - (extranonce1_len + extranonce2_len);
        if (remainder_in_coinbase_2 > 0) {
            coinbase_2_offset = remainder_in_coinbase_2;
        }
    }

    // 4. Parse Coinbase Suffix (nSequence, outputs, nLockTime)
    esp_err_t err = parse_coinbase_suffix(job, coinbase_2_offset,
                                          (const uint8_t (*)[MAX_SCRIPTPUBKEY_LEN])s_cached_scripts,
                                          s_cached_script_lens,
                                          user_script_count,
                                          bech32_hrp,
                                          is_testnet,
                                          decode_coinbase_tx,
                                          result);
    if (err != ESP_OK) {
        if (result->scriptsig) {
            free(result->scriptsig);
            result->scriptsig = NULL;
        }
        return err;
    }

    return ESP_OK;
}
