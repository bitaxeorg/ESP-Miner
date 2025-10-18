#ifndef STRATUM_UTILS_H
#define STRATUM_UTILS_H

#include <stddef.h>
#include <stdint.h>

size_t bin2hex(const uint8_t *buf, size_t buflen, char *hex, size_t hexlen);

void flip80bytes(void *dest_p, const void *src_p);
void flip32bytes(void *dest_p, const void *src_p);

size_t hex2bin(const char *hex, uint8_t *bin, size_t bin_len);

void print_hex(const uint8_t *b, size_t len,
               const size_t in_line, const char *prefix);

char *double_sha256(const char *hex_string);

void double_sha256_bin(const uint8_t *data, const size_t data_len, uint8_t dest[32]);

void single_sha256_bin(const uint8_t *data, const size_t data_len, uint8_t dest[32]);
void midstate_sha256_bin(const uint8_t *data, const size_t data_len, uint8_t dest[32]);

void swap_endian_words(const char *hex, uint8_t *output);

void reverse_bytes(uint8_t *data, size_t len);

double le256todouble(const void *target);

void prettyHex(unsigned char *buf, int len);

uint32_t flip32(uint32_t val);

double networkDifficulty(uint32_t nBits);

void suffixString(uint64_t val, char * buf, size_t bufsiz, int sigdigits);

#define STRATUM_DEFAULT_VERSION_MASK 0x1fffe000

#endif // STRATUM_UTILS_H
