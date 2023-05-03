#include "utils.h"

#include <string.h>
#include <stdio.h>

#include "mbedtls/sha256.h"


int hex2char(uint8_t x, char *c)
{
    if (x <= 9) {
        *c = x + '0';
    } else  if (x <= 15) {
        *c = x - 10 + 'a';
    } else {
        return -1;
    }

    return 0;
}

size_t bin2hex(const uint8_t *buf, size_t buflen, char *hex, size_t hexlen)
{
    if ((hexlen + 1) < buflen * 2) {
        return 0;
    }

    for (size_t i = 0; i < buflen; i++) {
        if (hex2char(buf[i] >> 4, &hex[2 * i]) < 0) {
            return 0;
        }
        if (hex2char(buf[i] & 0xf, &hex[2 * i + 1]) < 0) {
            return 0;
        }
    }

    hex[2 * buflen] = '\0';
    return 2 * buflen;
}

uint8_t hex2val(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    } else if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    } else {
        return 0;
    }
}

size_t hex2bin(const char *hex, uint8_t *bin, size_t bin_len)
{
    size_t len = 0;

    while (*hex && len < bin_len) {
        bin[len] = hex2val(*hex++) << 4;

        if (!*hex) {
            len++;
            break;
        }

        bin[len++] |= hex2val(*hex++);
    }

    return len;
}

static void print_hex( const uint8_t *b, size_t len,
                const size_t in_line, const char *prefix )
{
    size_t i = 0;
    const uint8_t *end = b + len;

    if( prefix == NULL )
    {
        prefix = "";
    }

    while( b < end )
    {
        if( ++i > in_line )
        {
            printf( "\n%s", prefix );
            i = 1;
        }
        printf( "%02X ", (uint8_t) *b++ );
    }
    printf("\n");
    fflush(stdout);
}

char * double_sha256(const char * hex_string)
{
    size_t bin_len = strlen(hex_string) / 2;
    uint8_t * bin = malloc(bin_len);
    hex2bin(hex_string, bin, bin_len);

    unsigned char first_hash_output[32], second_hash_output[32];

    mbedtls_sha256(bin, bin_len, first_hash_output, 0);
    mbedtls_sha256(first_hash_output, 32, second_hash_output, 0);

    free(bin);

    char * output_hash = malloc(64 + 1);
    bin2hex(second_hash_output, 32, output_hash, 65);
    return output_hash;
}

uint8_t * double_sha256_bin(const uint8_t * data, const size_t data_len)
{
    uint8_t first_hash_output[32];
    uint8_t * second_hash_output = malloc(32);

    mbedtls_sha256(data, data_len, first_hash_output, 0);
    mbedtls_sha256(first_hash_output, 32, second_hash_output, 0);

    return second_hash_output;
}