#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "coinbase_decoder.h"

// Test varint decoding
void test_decode_varint_single_byte(void) {
    uint8_t data[] = {0x42};
    int offset = 0;
    uint64_t result = coinbase_decode_varint(data, &offset);
    TEST_ASSERT_EQUAL_UINT64(0x42, result);
    TEST_ASSERT_EQUAL_INT(1, offset);
}

void test_decode_varint_fd_format(void) {
    uint8_t data[] = {0xFD, 0x34, 0x12};  // 0x1234 in little-endian
    int offset = 0;
    uint64_t result = coinbase_decode_varint(data, &offset);
    TEST_ASSERT_EQUAL_UINT64(0x1234, result);
    TEST_ASSERT_EQUAL_INT(3, offset);
}

void test_decode_varint_fe_format(void) {
    uint8_t data[] = {0xFE, 0x78, 0x56, 0x34, 0x12};  // 0x12345678 in little-endian
    int offset = 0;
    uint64_t result = coinbase_decode_varint(data, &offset);
    TEST_ASSERT_EQUAL_UINT64(0x12345678, result);
    TEST_ASSERT_EQUAL_INT(5, offset);
}

void test_decode_varint_ff_format(void) {
    uint8_t data[] = {0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    int offset = 0;
    uint64_t result = coinbase_decode_varint(data, &offset);
    TEST_ASSERT_EQUAL_UINT64(0x0807060504030201ULL, result);
    TEST_ASSERT_EQUAL_INT(9, offset);
}

// Test address decoding from scriptPubKey
void test_decode_p2pkh_address(void) {
    // P2PKH: OP_DUP OP_HASH160 <20 bytes> OP_EQUALVERIFY OP_CHECKSIG
    uint8_t script[] = {
        0x76, 0xa9, 0x14,
        0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab,
        0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0x88, 0xac
    };
    char output[MAX_ADDRESS_STRING_LEN];
    
    coinbase_decode_address_from_scriptpubkey(script, sizeof(script), output, sizeof(output));
    
    TEST_ASSERT_EQUAL_STRING("P2PKH:89abcdef0123456789abcdef0123456789abcdef", output);
}

void test_decode_p2sh_address(void) {
    // P2SH: OP_HASH160 <20 bytes> OP_EQUAL
    uint8_t script[] = {
        0xa9, 0x14,
        0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34,
        0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78,
        0x87
    };
    char output[MAX_ADDRESS_STRING_LEN];
    
    coinbase_decode_address_from_scriptpubkey(script, sizeof(script), output, sizeof(output));
    
    TEST_ASSERT_EQUAL_STRING("P2SH:123456789abcdef0123456789abcdef012345678", output);
}

void test_decode_p2wpkh_address(void) {
    // P2WPKH: OP_0 <20 bytes>
    uint8_t script[] = {
        0x00, 0x14,
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11, 0x22, 0x33,
        0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd
    };
    char output[MAX_ADDRESS_STRING_LEN];
    
    coinbase_decode_address_from_scriptpubkey(script, sizeof(script), output, sizeof(output));
    
    TEST_ASSERT_EQUAL_STRING("P2WPKH:aabbccddeeff00112233445566778899aabbccdd", output);
}

void test_decode_p2wsh_address(void) {
    // P2WSH: OP_0 <32 bytes>
    uint8_t script[] = {
        0x00, 0x20,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
        0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
        0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
    };
    char output[MAX_ADDRESS_STRING_LEN];
    
    coinbase_decode_address_from_scriptpubkey(script, sizeof(script), output, sizeof(output));
    
    TEST_ASSERT_EQUAL_STRING("P2WSH:0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20", output);
}

void test_decode_p2tr_address(void) {
    // P2TR: OP_1 <32 bytes>
    uint8_t script[] = {
        0x51, 0x20,
        0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88,
        0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00,
        0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88,
        0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00
    };
    char output[MAX_ADDRESS_STRING_LEN];
    
    coinbase_decode_address_from_scriptpubkey(script, sizeof(script), output, sizeof(output));
    
    TEST_ASSERT_EQUAL_STRING("P2TR:ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100", output);
}


// Main test runner
void app_main(void) {
    UNITY_BEGIN();
    
    // Varint tests
    RUN_TEST(test_decode_varint_single_byte);
    RUN_TEST(test_decode_varint_fd_format);
    RUN_TEST(test_decode_varint_fe_format);
    RUN_TEST(test_decode_varint_ff_format);
    
    // Address decoding tests
    RUN_TEST(test_decode_p2pkh_address);
    RUN_TEST(test_decode_p2sh_address);
    RUN_TEST(test_decode_p2wpkh_address);
    RUN_TEST(test_decode_p2wsh_address);
    RUN_TEST(test_decode_p2tr_address);
    
    UNITY_END();
}
