#include "utils.h"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include "esp_attr.h"
#include "esp_psram.h"
#include "esp_heap_caps.h"

#include "psa/crypto.h"

DRAM_ATTR static const char hex_table[] = "0123456789abcdef";

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
DRAM_ATTR const int8_t hex_val_table[256] = {
    [0 ... 255] = -1,
    ['0'] = 0,  ['1'] = 1,  ['2'] = 2,  ['3'] = 3,  ['4'] = 4,
    ['5'] = 5,  ['6'] = 6,  ['7'] = 7,  ['8'] = 8,  ['9'] = 9,
    ['a'] = 10, ['b'] = 11, ['c'] = 12, ['d'] = 13, ['e'] = 14, ['f'] = 15,
    ['A'] = 10, ['B'] = 11, ['C'] = 12, ['D'] = 13, ['E'] = 14, ['F'] = 15
};
#pragma GCC diagnostic pop

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
    if (hex == NULL || bin == NULL) {
        return 0;
    }

    size_t len = 0;
    while (len < bin_len && *hex != '\0') {
        int byte = hex_decode_byte(hex);
        if (byte < 0) {
            return 0;
        }

        bin[len++] = (uint8_t)byte;
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

bool sha256_bin(const uint8_t *data, size_t data_len, uint8_t dest[32])
{
    size_t output_len = 0;
    psa_status_t status = psa_hash_compute(PSA_ALG_SHA_256, data, data_len,
                                           dest, 32, &output_len);
    if (status != PSA_SUCCESS || output_len != 32) {
        memset(dest, 0xff, 32); // fail-close, never lower than difficulty target
        return false;
    }
    return true;
}

bool double_sha256_bin(const uint8_t *data, const size_t data_len, uint8_t dest[32])
{
    uint8_t first_hash_output[32];
    if (!sha256_bin(data, data_len, first_hash_output)) {
        memset(dest, 0xff, 32); // fail-close, never lower than difficulty target
        return false;
    }
    return sha256_bin(first_hash_output, sizeof(first_hash_output), dest);
}

void nbits_to_target(uint32_t nbits, uint8_t target[32])
{
    memset(target, 0, 32);
    if (nbits & 0x00800000) {
        return;
    }
    uint32_t mantissa = nbits & 0x007fffff;
    uint8_t exponent = (nbits >> 24) & 0xff;

    if (exponent <= 3) {
        mantissa >>= 8 * (3 - exponent);
        memcpy(target, &mantissa, 4);
    } else {
        size_t offset = exponent - 3;
        if (offset <= 28) {
            memcpy(target + offset, &mantissa, 4);
        } else if (offset < 32) {
            size_t copy_len = 32 - offset;
            memcpy(target + offset, &mantissa, copy_len);
        }
    }
}

void diff_to_target(double diff, uint8_t target[32])
{
    memset(target, 0, 32);
    if (diff <= 0.0 || isnan(diff)) return;

    double d64 = TRUEDIFFONE / diff;

    double dcut64 = d64 / BITS192;
    uint64_t h64 = (uint64_t)dcut64;
    for (int i = 0; i < 8; i++) target[24 + i] = (uint8_t)(h64 >> (i * 8));
    d64 -= ((double)h64) * BITS192;

    dcut64 = d64 / BITS128;
    h64 = (uint64_t)dcut64;
    for (int i = 0; i < 8; i++) target[16 + i] = (uint8_t)(h64 >> (i * 8));
    d64 -= ((double)h64) * BITS128;

    dcut64 = d64 / BITS64;
    h64 = (uint64_t)dcut64;
    for (int i = 0; i < 8; i++) target[8 + i] = (uint8_t)(h64 >> (i * 8));
    d64 -= ((double)h64) * BITS64;

    h64 = (uint64_t)d64;
    for (int i = 0; i < 8; i++) target[i] = (uint8_t)(h64 >> (i * 8));
}

double target_to_diff(const uint8_t target[32])
{
    if (!target) return (double)UINT32_MAX;

    uint64_t w0, w1, w2, w3;
    memcpy(&w0, target,      8);
    memcpy(&w1, target + 8,  8);
    memcpy(&w2, target + 16, 8);
    memcpy(&w3, target + 24, 8);

    if ((w0 | w1 | w2 | w3) == 0) return (double)UINT32_MAX;

    double s64 = (double)w3 * BITS192 + (double)w2 * BITS128 + (double)w1 * BITS64 + (double)w0;
    return TRUEDIFFONE / s64;
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

void url_decode(char *dst, const char *src)
{
    if (dst == NULL || src == NULL) {
        return;
    }

    while (*src != '\0') {
        if (*src == '%' && src[1] != '\0') {
            int byte = hex_decode_byte(src + 1);
            if (byte >= 0) {
                *dst++ = (char)byte;
                src += 3;
                continue;
            }
        }

        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

char *strdup_psram(const char *str)
{
    if (!str) return NULL;
    if (esp_psram_is_initialized()) {
        char *p = heap_caps_malloc(strlen(str) + 1, MALLOC_CAP_SPIRAM);
        if (p) {
            strcpy(p, str);
            return p;
        }
    }
    return strdup(str);
}
