#include "utils.h"

#include <string.h>
#include <stdio.h>
#include <math.h>

#include "mbedtls/sha256.h"

#define HASH_CNT_LSB 0x100000000uLL // 2^32 hashes for difficulty 1

static const char hex_table[] = "0123456789abcdef";

static const uint8_t hex_val_table[256] = {
    ['0'] = 0, ['1'] = 1, ['2'] = 2, ['3'] = 3, ['4'] = 4,
    ['5'] = 5, ['6'] = 6, ['7'] = 7, ['8'] = 8, ['9'] = 9,
    ['a'] = 10, ['b'] = 11, ['c'] = 12, ['d'] = 13, ['e'] = 14, ['f'] = 15,
    ['A'] = 10, ['B'] = 11, ['C'] = 12, ['D'] = 13, ['E'] = 14, ['F'] = 15
};

size_t bin2hex(const uint8_t *buf, size_t buflen, char *hex, size_t hexlen)
{
    if (hexlen <= buflen * 2) {
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

void double_sha256_bin(const uint8_t *data, const size_t data_len, uint8_t dest[32])
{
    uint8_t first_hash_output[32];

    mbedtls_sha256(data, data_len, first_hash_output, 0);
    mbedtls_sha256(first_hash_output, 32, dest, 0);
}

void midstate_sha256_bin(const uint8_t *data, const size_t data_len, uint8_t dest[32])
{
    mbedtls_sha256_context ctx;

    // Calculate midstate
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, data, 64);

    memcpy(dest, ctx.state, 32);

    mbedtls_sha256_free(&ctx);
}

void reverse_32bit_words(const uint8_t src[32], uint8_t dest[32])
{
    const uint32_t *s = (const uint32_t *)src;
    uint32_t *d = (uint32_t *)dest;
    
    d[0] = s[7];
    d[1] = s[6];
    d[2] = s[5];
    d[3] = s[4];
    d[4] = s[3];
    d[5] = s[2];
    d[6] = s[1];
    d[7] = s[0];    
}

void reverse_endianness_per_word(uint8_t data[32])
{
    uint32_t *d = (uint32_t *)data;

    d[0] = __builtin_bswap32(d[0]);
    d[1] = __builtin_bswap32(d[1]);
    d[2] = __builtin_bswap32(d[2]);
    d[3] = __builtin_bswap32(d[3]);
    d[4] = __builtin_bswap32(d[4]);
    d[5] = __builtin_bswap32(d[5]);
    d[6] = __builtin_bswap32(d[6]);
    d[7] = __builtin_bswap32(d[7]);
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
            snprintf(buf, bufsiz, "%.2f%s", dval, suffix);
        else
            snprintf(buf, bufsiz, "%d%s", (unsigned int) dval, suffix);
    } else {
        /* Always show sigdigits + 1, padded on right with zeroes
         * followed by suffix */
        int ndigits = sigdigits - 1 - (dval > 0.0 ? floor(log10(dval)) : 0);

        snprintf(buf, bufsiz, "%*.*f%s", sigdigits + 1, ndigits, dval, suffix);
    }
}

float hashCounterToGhs(uint32_t duration_ms, uint32_t counter)
{
    if (duration_ms == 0) return 0.0f;
    float seconds = duration_ms / 1000.0;
    float hashrate = counter / seconds * (float)HASH_CNT_LSB; // Make sure it stays in float
    return hashrate / 1e9f; // Convert to Gh/s
}
