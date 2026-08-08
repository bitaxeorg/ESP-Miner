#include "utils.h"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include "esp_psram.h"
#include "esp_heap_caps.h"

#include "psa/crypto.h"

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

void sha256_bin(const uint8_t *data, size_t data_len, uint8_t dest[32])
{
    size_t output_len = 0;
    psa_status_t status = psa_hash_compute(PSA_ALG_SHA_256, data, data_len,
                                           dest, 32, &output_len);
    if (status != PSA_SUCCESS || output_len != 32) {
        memset(dest, 0, 32);
    }
}

void double_sha256_bin(const uint8_t *data, const size_t data_len, uint8_t dest[32])
{
    uint8_t first_hash_output[32];
    sha256_bin(data, data_len, first_hash_output);
    sha256_bin(first_hash_output, sizeof(first_hash_output), dest);
}

static inline uint32_t sha256_rotr(uint32_t value, unsigned shift)
{
    return (value >> shift) | (value << (32 - shift));
}

static uint32_t sha256_load_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

static void sha256_store_be32(uint32_t value, uint8_t *dest)
{
    dest[0] = (uint8_t)(value >> 24);
    dest[1] = (uint8_t)(value >> 16);
    dest[2] = (uint8_t)(value >> 8);
    dest[3] = (uint8_t)value;
}

static const uint32_t sha256_round_constants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static const uint32_t sha256_initial_state[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

void midstate_sha256_bin(const uint8_t *data, const size_t data_len, uint8_t dest[32])
{
    if (data == NULL || data_len != 64) {
        memset(dest, 0, 32);
        return;
    }

    uint32_t schedule[64];
    for (int i = 0; i < 16; i++) {
        schedule[i] = sha256_load_be32(data + (i * 4));
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = sha256_rotr(schedule[i - 15], 7) ^
                      sha256_rotr(schedule[i - 15], 18) ^
                      (schedule[i - 15] >> 3);
        uint32_t s1 = sha256_rotr(schedule[i - 2], 17) ^
                      sha256_rotr(schedule[i - 2], 19) ^
                      (schedule[i - 2] >> 10);
        schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
    }

    uint32_t a = sha256_initial_state[0];
    uint32_t b = sha256_initial_state[1];
    uint32_t c = sha256_initial_state[2];
    uint32_t d = sha256_initial_state[3];
    uint32_t e = sha256_initial_state[4];
    uint32_t f = sha256_initial_state[5];
    uint32_t g = sha256_initial_state[6];
    uint32_t h = sha256_initial_state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t sum1 = sha256_rotr(e, 6) ^ sha256_rotr(e, 11) ^ sha256_rotr(e, 25);
        uint32_t choose = (e & f) ^ (~e & g);
        uint32_t temp1 = h + sum1 + choose + sha256_round_constants[i] + schedule[i];
        uint32_t sum0 = sha256_rotr(a, 2) ^ sha256_rotr(a, 13) ^ sha256_rotr(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = sum0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    const uint32_t working[8] = { a, b, c, d, e, f, g, h };
    for (int i = 0; i < 8; i++) {
        sha256_store_be32(sha256_initial_state[i] + working[i], dest + i * 4);
    }
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

const double truediffone = 26959535291011309493156476344723991336010898738574164086137773096960.0;
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

float hashCounterToGhs(uint64_t duration_us, uint32_t counter)
{
    if (duration_us == 0) return 0.0f;
    float seconds = duration_us / 1000000.0;
    float hashrate = counter / seconds * (float)HASH_CNT_LSB; // Make sure it stays in float
    return hashrate / 1e9f; // Convert to Gh/s
}

void url_decode(char *dst, const char *src) {
    while (*src) {
        if ((*src == '%') && src[1] && src[2]) {
            *dst++ = (hex_val_table[(unsigned char)src[1]] << 4) | hex_val_table[(unsigned char)src[2]];
            src += 3;
        } else if (*src == '+') {
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
