#ifndef STRATUM_UTILS_H
#define STRATUM_UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Bitcoin Difficulty 1 target: 0x00000000ffff0000...0000 (0xffff * 2^208) */
#define TRUEDIFFONE 0xffff.0p208

/* Scale factors (2^64, 2^128, 2^192) for 64-bit limbs of 128/256-bit integers */
#define BITS64  0x1.0p64
#define BITS128 0x1.0p128
#define BITS192 0x1.0p192

// BIP320 16-bit version rolling mask (bits 13..28: 0x1fffe000).
// BM13xx ASICs program version rolling as a 16-bit field shifted by 13 (version_mask >> 13).
// This is a strict hardware-compatible subset of the BIP323 mask.
#define BIP320_VERSION_ROLLING_MASK 0x1fffe000U

extern const int8_t hex_val_table[256];

/**
 * @brief Decode two hex ASCII characters into a single byte.
 * @param hex Pointer to hex characters.
 * @return Decoded byte value (0..255), or -1 if invalid or odd-length.
 */
static inline int hex_decode_byte(const char *hex)
{
    if (hex == NULL || hex[0] == '\0' || hex[1] == '\0') {
        return -1;
    }
    int high = hex_val_table[(unsigned char)hex[0]];
    int low  = hex_val_table[(unsigned char)hex[1]];
    if ((high | low) < 0) {
        return -1;
    }
    return (high << 4) | low;
}

size_t bin2hex(const uint8_t *buf, size_t buflen, char *hex, size_t hexlen);

size_t hex2bin(const char *hex, uint8_t *bin, size_t bin_len);

void print_hex(const uint8_t *b, size_t len,
               const size_t in_line, const char *prefix);

bool sha256_bin(const uint8_t *data, size_t data_len, uint8_t dest[32]);

bool double_sha256_bin(const uint8_t *data, const size_t data_len, uint8_t dest[32]);

/* Hash byte helpers accept buffers at any byte alignment. Source and
 * destination must not overlap for reverse_32bit_words(). */
void reverse_32bit_words(const uint8_t src[32], uint8_t dest[32]);

void reverse_endianness_per_word(uint8_t data[32]);

void prettyHex(unsigned char *buf, int len);

void suffixString(uint64_t val, char * buf, size_t bufsiz, int sigdigits);

void url_decode(char *dst, const char *src);

char *strdup_psram(const char *str);

/**
 * @brief Expands Bitcoin compact nBits (32-bit) into a 256-bit target (little-endian byte array).
 * @param nbits 32-bit compact target from block header (exponent in high byte, 24-bit mantissa).
 * @param target 32-byte output buffer (little-endian: byte 0 is LSB, byte 31 is MSB).
 */
void nbits_to_target(uint32_t nbits, uint8_t target[32]);

/**
 * @brief Converts a floating-point difficulty into a 256-bit share target.
 * target = truediffone / diff
 * @param diff Difficulty value (> 0.0).
 * @param target 32-byte output buffer (little-endian).
 */
void diff_to_target(double diff, uint8_t target[32]);

/**
 * @brief Converts a 256-bit target (or hash) into a floating-point difficulty.
 * diff = truediffone / target
 * @param target 32-byte target or hash buffer (little-endian).
 * @return Difficulty value, or UINT32_MAX on invalid/zero target.
 */
double target_to_diff(const uint8_t target[32]);

/**
 * @brief Compares two 256-bit little-endian integers.
 * Evaluates whether hash <= target.
 * @param hash 32-byte hash buffer (little-endian: byte 31 is MSB).
 * @param target 32-byte target buffer (little-endian: byte 31 is MSB).
 * @return true if hash <= target (PoW valid), false otherwise.
 */
static inline bool uint256_lte(const uint8_t hash[32], const uint8_t target[32])
{
    for (int i = 31; i >= 0; i--) {
        if (hash[i] < target[i]) return true;
        if (hash[i] > target[i]) return false;
    }
    return true; // Exactly equal
}

#endif // STRATUM_UTILS_H
