#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "libbase58.h"
#include "utils.h"
#include "mbedtls/sha256.h"

// Wrapper for SHA256 to match libbase58's expected signature
static bool my_sha256(void *digest, const void *data, size_t datasz) {
    mbedtls_sha256(data, datasz, digest, 0);
    return true;
}

void setUp(void) {
    b58_sha256_impl = my_sha256;
}

void tearDown(void) {
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

TEST_CASE("Base58 buffer too small", "[base58]")
{
    uint8_t hash[20] = {0};
    char output[10]; // Too small
    size_t outsz = sizeof(output);
    
    bool result = b58check_enc(output, &outsz, 0x00, hash, 20);
    
    TEST_ASSERT_FALSE(result);
}
