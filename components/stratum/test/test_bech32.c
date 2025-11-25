#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "segwit_addr.h"

// Test P2WPKH encoding (witness v0, 20 bytes)
void test_bech32_p2wpkh_encoding(void) {
    uint8_t hash[20] = {
        0x80, 0xbd, 0xed, 0x37, 0xe3, 0xf8, 0x6a, 0x1a, 
        0x54, 0x6e, 0x09, 0x9b, 0x15, 0xe2, 0xb0, 0x28, 
        0x55, 0xd3, 0x84, 0x3c
    };
    char output[90];
    
    int result = segwit_addr_encode(output, "bc", 0, hash, 20);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_STRING("bc1qsz776dlrlp4p54rwpxd3tc4s9p2a8ppuceyvdc", output);
}

// Test P2WSH encoding (witness v0, 32 bytes)
void test_bech32_p2wsh_encoding(void) {
    uint8_t hash[32] = {
        0x18, 0x63, 0x14, 0x3c, 0x14, 0xc5, 0x16, 0x68,
        0x04, 0xbd, 0x19, 0x20, 0x33, 0x56, 0xda, 0x13,
        0x6c, 0x98, 0x56, 0x78, 0xcd, 0x4d, 0x27, 0xa1,
        0xb8, 0xc6, 0x32, 0x96, 0x04, 0x90, 0x32, 0x62
    };
    char output[90];
    
    int result = segwit_addr_encode(output, "bc", 0, hash, 32);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_STRING("bc1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3qccfmv3", output);
}

// Test P2TR encoding (witness v1, 32 bytes) - Bech32m
void test_bech32m_p2tr_encoding(void) {
    uint8_t hash[32] = {
        0x53, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    char output[90];
    
    int result = segwit_addr_encode(output, "bc", 1, hash, 32);
    
    TEST_ASSERT_TRUE(result);
    // P2TR uses Bech32m (different checksum)
    TEST_ASSERT_EQUAL_STRING("bc1p2vfqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq3yqwwx", output);
}

// Test invalid witness version
void test_bech32_invalid_version(void) {
    uint8_t hash[20] = {0x00};
    char output[90];
    
    int result = segwit_addr_encode(output, "bc", 17, hash, 20);
    
    TEST_ASSERT_FALSE(result);
}

// Test invalid program length for v0
void test_bech32_invalid_v0_length(void) {
    uint8_t hash[25] = {0x00};
    char output[90];
    
    int result = segwit_addr_encode(output, "bc", 0, hash, 25);
    
    TEST_ASSERT_FALSE(result);
}

// Test testnet encoding
void test_bech32_testnet(void) {
    uint8_t hash[20] = {
        0x00, 0x14, 0x75, 0x1e, 0x76, 0xe8, 0x19, 0x91,
        0x96, 0xd4, 0x54, 0x94, 0x1c, 0x45, 0xd1, 0xb3,
        0xa3, 0x23, 0xf1, 0x43
    };
    char output[90];
    
    int result = segwit_addr_encode(output, "tb", 0, hash, 20);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_STRING("tb1qqq2w0m5pjyvjd4y5fz2u6gd48g3r8c2r93lt5v", output);
}

// Main test runner
void app_main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_bech32_p2wpkh_encoding);
    RUN_TEST(test_bech32_p2wsh_encoding);
    RUN_TEST(test_bech32m_p2tr_encoding);
    RUN_TEST(test_bech32_invalid_version);
    RUN_TEST(test_bech32_invalid_v0_length);
    RUN_TEST(test_bech32_testnet);
    
    UNITY_END();
}
