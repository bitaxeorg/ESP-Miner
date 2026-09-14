#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "libbase58.h"
#include "utils.h"

// Wrapper for SHA256 to match libbase58's expected signature
static bool my_sha256(void *digest, const void *data, size_t datasz) {
    sha256_bin(data, datasz, digest);
    return true;
}

static bool failing_sha256(void *digest, const void *data, size_t datasz) {
    (void)digest;
    (void)data;
    (void)datasz;
    return false;
}

void setUp(void) {
    b58_sha256_impl = my_sha256;
}

void tearDown(void) {
    b58_sha256_impl = my_sha256;
}

typedef struct {
    const uint8_t *binary;
    size_t binary_size;
    const char *base58;
} base58_vector_t;

// Independent known-answer vectors from Bitcoin Core. Testing both directions
// against fixed answers prevents the encoder and decoder from hiding the same bug.
// https://github.com/bitcoin/bitcoin/blob/master/src/test/data/base58_encode_decode.json
static const uint8_t RAW_61[] = {0x61};
static const uint8_t RAW_626262[] = {0x62, 0x62, 0x62};
static const uint8_t RAW_LONG_STRING[] = "simply a long string";
static const uint8_t RAW_LEADING_ZERO[] = {
    0x00, 0xeb, 0x15, 0x23, 0x1d, 0xfc, 0xeb, 0x60, 0x92, 0x58, 0x86, 0xb6,
    0x7d, 0x06, 0x52, 0x99, 0x92, 0x59, 0x15, 0xae, 0xb1, 0x72, 0xc0, 0x66,
    0x47,
};
static const uint8_t RAW_TEN_ZEROS[10] = {0};
static const uint8_t RAW_BEFORE_CARRY[] = {0x27, 0x1f, 0x35, 0x9f};
static const uint8_t RAW_AFTER_CARRY[] = {0x27, 0x1f, 0x35, 0xa0};

static const base58_vector_t BASE58_VECTORS[] = {
    {RAW_61, sizeof(RAW_61), "2g"},
    {RAW_626262, sizeof(RAW_626262), "a3gV"},
    {RAW_LONG_STRING, sizeof(RAW_LONG_STRING) - 1, "2cFupjhnEsSn59qHXstmK2ffpLv2"},
    {RAW_LEADING_ZERO, sizeof(RAW_LEADING_ZERO), "1NS17iag9jJgTHD1VXjvLCEnZuQ3rJDE9L"},
    {RAW_TEN_ZEROS, sizeof(RAW_TEN_ZEROS), "1111111111"},
    {RAW_BEFORE_CARRY, sizeof(RAW_BEFORE_CARRY), "zzzzz"},
    {RAW_AFTER_CARRY, sizeof(RAW_AFTER_CARRY), "211111"},
};

TEST_CASE("Base58 matches independent known-answer vectors", "[base58]")
{
    for (size_t index = 0; index < sizeof(BASE58_VECTORS) / sizeof(BASE58_VECTORS[0]); ++index) {
        const base58_vector_t *vector = &BASE58_VECTORS[index];
        char encoded[64];
        size_t encoded_size = sizeof(encoded);

        TEST_ASSERT_TRUE(b58enc(encoded, &encoded_size, vector->binary, vector->binary_size));
        TEST_ASSERT_EQUAL_STRING(vector->base58, encoded);
        TEST_ASSERT_EQUAL_UINT(strlen(vector->base58) + 1, encoded_size);

        uint8_t decoded[32] = {0};
        size_t decoded_size = vector->binary_size;
        TEST_ASSERT_TRUE(b58tobin(decoded, &decoded_size, vector->base58, 0));
        TEST_ASSERT_EQUAL_UINT(vector->binary_size, decoded_size);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(vector->binary, decoded, vector->binary_size);
    }
}

TEST_CASE("Base58 P2PKH encoding", "[base58]")
{
    uint8_t hash[20] = {
        0x62, 0xe9, 0x07, 0xb1, 0x5c, 0xbf, 0x27, 0xd5, 0x42, 0x53,
        0x99, 0xeb, 0xf6, 0xf0, 0xfb, 0x50, 0xeb, 0xb8, 0x8f, 0x18
    };
    char output[50];
    size_t outsz = sizeof(output);
    
    bool result = b58check_enc(output, &outsz, 0x00, hash, 20);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_STRING("1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa", output); // Genesis address
}

TEST_CASE("Base58 P2SH encoding", "[base58]")
{
    uint8_t hash[20] = {
        0xb4, 0x72, 0xa2, 0x66, 0xd0, 0xbd, 0x89, 0xc1, 0x37, 0x06,
        0xa4, 0x13, 0x2c, 0xcf, 0xb1, 0x6f, 0x7c, 0x3b, 0x9f, 0xcb
    };
    char output[50];
    size_t outsz = sizeof(output);
    
    bool result = b58check_enc(output, &outsz, 0x05, hash, 20);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_STRING("3J98t1WpEZ73CNmQviecrnyiWrnqRhWNLy", output);
}

TEST_CASE("Base58 P2PKH decoding", "[base58]")
{
    static const uint8_t expected_hash[20] = {
        0x62, 0xe9, 0x07, 0xb1, 0x5c, 0xbf, 0x27, 0xd5, 0x42, 0x53,
        0x99, 0xeb, 0xf6, 0xf0, 0xfb, 0x50, 0xeb, 0xb8, 0x8f, 0x18
    };
    static const char address[] = "1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa";
    uint8_t decoded[25];
    size_t decoded_size = sizeof(decoded);

    TEST_ASSERT_TRUE(b58tobin(decoded, &decoded_size, address, 0));
    TEST_ASSERT_EQUAL_UINT32(sizeof(decoded), decoded_size);
    TEST_ASSERT_EQUAL_UINT8(0, decoded[0]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_hash, decoded + 1, sizeof(expected_hash));
    TEST_ASSERT_EQUAL_INT(0, b58check(decoded, decoded_size, address, 0));
}

TEST_CASE("Base58 buffer too small", "[base58]")
{
    uint8_t hash[20] = {0};
    char output[10]; // Too small
    size_t outsz = sizeof(output);
    
    bool result = b58check_enc(output, &outsz, 0x00, hash, 20);
    
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_TRUE(outsz > sizeof(output));
}

TEST_CASE("Base58 reports the exact output size", "[base58]")
{
    static const uint8_t value[] = {0x61};
    char output[3];
    size_t output_size = sizeof(output) - 1;

    TEST_ASSERT_FALSE(b58enc(output, &output_size, value, sizeof(value)));
    TEST_ASSERT_EQUAL_UINT(sizeof(output), output_size);

    output_size = sizeof(output);
    TEST_ASSERT_TRUE(b58enc(output, &output_size, value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("2g", output);
    TEST_ASSERT_EQUAL_UINT(sizeof(output), output_size);
}

TEST_CASE("Base58 decoder honors explicit input length", "[base58]")
{
    uint8_t output = 0;
    size_t output_size = sizeof(output);

    TEST_ASSERT_TRUE(b58tobin(&output, &output_size, "2g0", 2));
    TEST_ASSERT_EQUAL_HEX8(0x61, output);
    TEST_ASSERT_EQUAL_UINT(1, output_size);

    output_size = sizeof(output);
    TEST_ASSERT_FALSE(b58tobin(&output, &output_size, "2g0", 0));
}

TEST_CASE("Base58 decoder rejects invalid alphabet and overflow", "[base58]")
{
    static const char *invalid_values[] = {"0", "O", "I", "l"};
    uint8_t output[4];

    for (size_t index = 0; index < sizeof(invalid_values) / sizeof(invalid_values[0]); ++index) {
        size_t output_size = sizeof(output);
        TEST_ASSERT_FALSE(b58tobin(output, &output_size, invalid_values[index], 0));
    }

    char high_bit_value[] = {(char)0x80, '\0'};
    size_t output_size = sizeof(output);
    TEST_ASSERT_FALSE(b58tobin(output, &output_size, high_bit_value, 0));

    output_size = 1;
    TEST_ASSERT_FALSE(b58tobin(output, &output_size, "zz", 0));
}

TEST_CASE("Base58Check matches an independent all-zero hash vector", "[base58]")
{
    // Published independently by Nayuki's Bitcoin Cryptography Library:
    // https://github.com/nayuki/Bitcoin-Cryptography-Library/blob/master/cpp/Base58CheckTest.cpp
    static const char expected[] = "1111111111111111111114oLvT2";
    uint8_t hash[20] = {0};
    char encoded[40];
    size_t encoded_size = sizeof(encoded);

    TEST_ASSERT_TRUE(b58check_enc(encoded, &encoded_size, 0, hash, sizeof(hash)));
    TEST_ASSERT_EQUAL_STRING(expected, encoded);
    TEST_ASSERT_EQUAL_UINT(strlen(expected) + 1, encoded_size);

    uint8_t decoded[25];
    uint8_t expected_payload[21] = {0};
    size_t decoded_size = sizeof(decoded);
    TEST_ASSERT_TRUE(b58tobin(decoded, &decoded_size, expected, 0));
    TEST_ASSERT_EQUAL_UINT(sizeof(decoded), decoded_size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_payload, decoded, sizeof(expected_payload));
    TEST_ASSERT_EQUAL_INT(0, b58check(decoded, decoded_size, expected, 0));
}

TEST_CASE("Base58Check rejects bad checksums and noncanonical zeroes", "[base58]")
{
    static const char valid[] = "1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa";
    static const char bad_checksum[] = "1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNb";
    static const char extra_zero[] = "11A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa";
    uint8_t decoded[25];
    size_t decoded_size = sizeof(decoded);

    TEST_ASSERT_TRUE(b58tobin(decoded, &decoded_size, bad_checksum, 0));
    TEST_ASSERT_EQUAL_INT(-1, b58check(decoded, decoded_size, bad_checksum, 0));

    decoded_size = sizeof(decoded);
    TEST_ASSERT_TRUE(b58tobin(decoded, &decoded_size, valid, 0));
    TEST_ASSERT_EQUAL_INT(-3, b58check(decoded, decoded_size, extra_zero, 0));
    TEST_ASSERT_EQUAL_INT(-4, b58check(decoded, 3, valid, 0));
}

TEST_CASE("Base58Check propagates SHA-256 failure", "[base58]")
{
    uint8_t data[20] = {0};
    char encoded[40];
    size_t encoded_size = sizeof(encoded);
    b58_sha256_impl = failing_sha256;

    TEST_ASSERT_FALSE(b58check_enc(encoded, &encoded_size, 0, data, sizeof(data)));
    TEST_ASSERT_EQUAL_UINT(0, encoded_size);
    TEST_ASSERT_EQUAL_INT(-2, b58check(data, sizeof(data), "1", 0));
}
