#include "utils.h"

#include <string.h>
#include <stdio.h>
#include <math.h>

#include "mbedtls/sha256.h"

static const char hex_table[] = "0123456789abcdef";

static const uint8_t hex_val_table[256] = {
    ['0'] = 0, ['1'] = 1, ['2'] = 2, ['3'] = 3, ['4'] = 4,
    ['5'] = 5, ['6'] = 6, ['7'] = 7, ['8'] = 8, ['9'] = 9,
    ['a'] = 10, ['b'] = 11, ['c'] = 12, ['d'] = 13, ['e'] = 14, ['f'] = 15,
    ['A'] = 10, ['B'] = 11, ['C'] = 12, ['D'] = 13, ['E'] = 14, ['F'] = 15
};

#ifndef bswap_16
#define bswap_16(a) ((((uint16_t)(a) << 8) & 0xff00) | (((uint16_t)(a) >> 8) & 0xff))
#endif

#ifndef bswap_32
#define bswap_32(a) ((((uint32_t)(a) << 24) & 0xff000000) | \
                     (((uint32_t)(a) << 8) & 0xff0000) |    \
                     (((uint32_t)(a) >> 8) & 0xff00) |      \
                     (((uint32_t)(a) >> 24) & 0xff))
#endif

/*
 * General byte order swapping functions.
 */
#define bswap16(x) __bswap16(x)
#define bswap32(x) __bswap32(x)
#define bswap64(x) __bswap64(x)

uint32_t swab32(uint32_t v)
{
    return bswap_32(v);
}

// takes 80 bytes and flips every 4 bytes
void flip80bytes(void *dest_p, const void *src_p)
{
    uint32_t *dest = dest_p;
    const uint32_t *src = src_p;
    int i;

    for (i = 0; i < 20; i++)
        dest[i] = swab32(src[i]);
}

void flip64bytes(void *dest_p, const void *src_p)
{
    uint32_t *dest = dest_p;
    const uint32_t *src = src_p;
    int i;

    for (i = 0; i < 16; i++)
        dest[i] = swab32(src[i]);
}

void flip32bytes(void *dest_p, const void *src_p)
{
    uint32_t *dest = dest_p;
    const uint32_t *src = src_p;
    int i;

    for (i = 0; i < 8; i++)
        dest[i] = swab32(src[i]);
}

size_t bin2hex(const uint8_t *buf, size_t buflen, char *hex, size_t hexlen)
{
    if (hexlen < buflen * 2) {
        return 0;
    }

    for (size_t i = 0; i < buflen; i++) {
        hex[2 * i] = hex_table[buf[i] >> 4];
        hex[2 * i + 1] = hex_table[buf[i] & 0x0F];
    }
    hex[2 * buflen] = '\0';
    return 2 * buflen;
}

size_t hex2bin(const char *hex, uint8_t *bin, size_t bin_len)
{
    size_t len = 0;

    while (len < bin_len && hex[0]) {
        if (!hex[1]) {
            bin[len++] = hex_val_table[(unsigned char)hex[0]] << 4;
            break;
        }
        bin[len++] = (hex_val_table[(unsigned char)hex[0]] << 4) | hex_val_table[(unsigned char)hex[1]];
        hex += 2;
    }

    return len;
}

void print_hex(const uint8_t *b, size_t len,
               const size_t in_line, const char *prefix)
{
    size_t i = 0;
    const uint8_t *end = b + len;

    if (prefix == NULL)
    {
        prefix = "";
    }

    printf("%s", prefix);
    while (b < end)
    {
        if (++i > in_line)
        {
            printf("\n%s", prefix);
            i = 1;
        }
        printf("%02X ", (uint8_t)*b++);
    }
    printf("\n");
    fflush(stdout);
}

char *double_sha256(const char *hex_string)
{
    size_t bin_len = strlen(hex_string) / 2;
    uint8_t *bin = malloc(bin_len);
    hex2bin(hex_string, bin, bin_len);

    unsigned char first_hash_output[32], second_hash_output[32];

    mbedtls_sha256(bin, bin_len, first_hash_output, 0);
    mbedtls_sha256(first_hash_output, 32, second_hash_output, 0);

    free(bin);

    char *output_hash = malloc(64 + 1);
    bin2hex(second_hash_output, 32, output_hash, 65);
    return output_hash;
}

void double_sha256_bin(const uint8_t *data, const size_t data_len, uint8_t dest[32])
{
    uint8_t first_hash_output[32];

    mbedtls_sha256(data, data_len, first_hash_output, 0);
    mbedtls_sha256(first_hash_output, 32, dest, 0);
}

void single_sha256_bin(const uint8_t *data, const size_t data_len, uint8_t dest[32])
{
    // mbedtls_sha256(data, data_len, dest, 0);

    // Initialize SHA256 context
    mbedtls_sha256_context sha256_ctx;
    mbedtls_sha256_init(&sha256_ctx);
    mbedtls_sha256_starts(&sha256_ctx, 0);

    // Compute first SHA256 hash of header
    mbedtls_sha256_update(&sha256_ctx, data, 64);
    unsigned char hash[32];
    mbedtls_sha256_finish(&sha256_ctx, hash);

    // Compute midstate from hash
    memcpy(dest, hash, 32);
}

void midstate_sha256_bin(const uint8_t *data, const size_t data_len, uint8_t dest[32])
{
    mbedtls_sha256_context midstate;

    // Calculate midstate
    mbedtls_sha256_init(&midstate);
    mbedtls_sha256_starts(&midstate, 0);
    mbedtls_sha256_update(&midstate, data, 64);

    // memcpy(dest, midstate.state, 32);
     flip32bytes(dest, midstate.state);
}

void swap_endian_words(const char *hex_words, uint8_t *output)
{
    size_t hex_length = strlen(hex_words);
    if (hex_length % 8 != 0)
    {
        fprintf(stderr, "Must be 4-byte word aligned\n");
        exit(EXIT_FAILURE);
    }

    size_t binary_length = hex_length / 2;

    for (size_t i = 0; i < binary_length; i += 4)
    {
        for (int j = 0; j < 4; j++)
        {
            unsigned int byte_val;
            sscanf(hex_words + (i + j) * 2, "%2x", &byte_val);
            output[i + (3 - j)] = byte_val;
        }
    }
}

void reverse_bytes(uint8_t *data, size_t len)
{
    for (int i = 0; i < len / 2; ++i)
    {
        uint8_t temp = data[i];
        data[i] = data[len - 1 - i];
        data[len - 1 - i] = temp;
    }
}

// static const double truediffone = 26959535291011309493156476344723991336010898738574164086137773096960.0;
static const double bits192 = 6277101735386680763835789423207666416102355444464034512896.0;
static const double bits128 = 340282366920938463463374607431768211456.0;
static const double bits64 = 18446744073709551616.0;

/* Converts a little endian 256 bit value to a double */
double le256todouble(const void *target)
{
    uint64_t *data64;
    double dcut64;

    data64 = (uint64_t *)(target + 24);
    dcut64 = *data64 * bits192;

    data64 = (uint64_t *)(target + 16);
    dcut64 += *data64 * bits128;

    data64 = (uint64_t *)(target + 8);
    dcut64 += *data64 * bits64;

    data64 = (uint64_t *)(target);
    dcut64 += *data64;

    return dcut64;
}

void prettyHex(unsigned char *buf, int len)
{
    int i;
    printf("[");
    for (i = 0; i < len - 1; i++)
    {
        printf("%02X ", buf[i]);
    }
    printf("%02X]", buf[len - 1]);
}

uint32_t flip32(uint32_t val)
{
    uint32_t ret = 0;
    ret |= (val & 0xFF) << 24;
    ret |= (val & 0xFF00) << 8;
    ret |= (val & 0xFF0000) >> 8;
    ret |= (val & 0xFF000000) >> 24;
    return ret;
}

/* Calculate the network difficulty from nBits */
double networkDifficulty(uint32_t nBits)
{
    uint32_t mantissa = nBits & 0x007fffff;  // Extract the mantissa from nBits
    uint8_t exponent = (nBits >> 24) & 0xff; // Extract the exponent from nBits

    double target = (double) mantissa * pow(256, (exponent - 3)); // Calculate the target value

    double difficulty = (pow(2, 208) * 65535) / target; // Calculate the difficulty

    return difficulty;
}

/* Convert a uint64_t value into a truncated string for displaying with its
 * associated suitable for Mega, Giga etc. Buf array needs to be long enough */
void suffixString(uint64_t val, char * buf, size_t bufsiz, int sigdigits)
{
    const double dkilo = 1000.0;
    const uint64_t kilo = 1000ull;
    const uint64_t mega = 1000000ull;
    const uint64_t giga = 1000000000ull;
    const uint64_t tera = 1000000000000ull;
    const uint64_t peta = 1000000000000000ull;
    const uint64_t exa = 1000000000000000000ull;
    char suffix[2] = "";
    bool decimal = true;
    double dval;

    if (val >= exa) {
        val /= peta;
        dval = (double) val / dkilo;
        strcpy(suffix, "E");
    } else if (val >= peta) {
        val /= tera;
        dval = (double) val / dkilo;
        strcpy(suffix, "P");
    } else if (val >= tera) {
        val /= giga;
        dval = (double) val / dkilo;
        strcpy(suffix, "T");
    } else if (val >= giga) {
        val /= mega;
        dval = (double) val / dkilo;
        strcpy(suffix, "G");
    } else if (val >= mega) {
        val /= kilo;
        dval = (double) val / dkilo;
        strcpy(suffix, "M");
    } else if (val >= kilo) {
        dval = (double) val / dkilo;
        strcpy(suffix, "k");
    } else {
        dval = val;
        decimal = false;
    }

    if (!sigdigits) {
        if (decimal)
            snprintf(buf, bufsiz, "%.2f %s", dval, suffix);
        else
            snprintf(buf, bufsiz, "%d %s", (unsigned int) dval, suffix);
    } else {
        /* Always show sigdigits + 1, padded on right with zeroes
         * followed by suffix */
        int ndigits = sigdigits - 1 - (dval > 0.0 ? floor(log10(dval)) : 0);

        snprintf(buf, bufsiz, "%*.*f %s", sigdigits + 1, ndigits, dval, suffix);
    }
}
