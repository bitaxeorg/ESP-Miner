#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "coinbase_decoder.h"
#include "stratum_api.h"
#include "utils.h"

TEST_CASE("Varint decode single byte", "[coinbase_decoder]")
{
    uint8_t data[] = {0x42};
    int offset = 0;
    uint64_t result = coinbase_decode_varint(data, sizeof(data), &offset);
    TEST_ASSERT_TRUE(0x42 == result);
    TEST_ASSERT_EQUAL_INT(1, offset);
}

TEST_CASE("Varint decode FD format", "[coinbase_decoder]")
{
    uint8_t data[] = {0xFD, 0x34, 0x12};  // 0x1234 in little-endian
    int offset = 0;
    uint64_t result = coinbase_decode_varint(data, sizeof(data), &offset);
    TEST_ASSERT_TRUE(0x1234 == result);
    TEST_ASSERT_EQUAL_INT(3, offset);
}

TEST_CASE("Varint decode FE format", "[coinbase_decoder]")
{
    uint8_t data[] = {0xFE, 0x78, 0x56, 0x34, 0x12};  // 0x12345678 in little-endian
    int offset = 0;
    uint64_t result = coinbase_decode_varint(data, sizeof(data), &offset);
    TEST_ASSERT_TRUE(0x12345678 == result);
    TEST_ASSERT_EQUAL_INT(5, offset);
}

TEST_CASE("Varint decode FF format", "[coinbase_decoder]")
{
    uint8_t data[] = {0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    int offset = 0;
    uint64_t result = coinbase_decode_varint(data, sizeof(data), &offset);
    TEST_ASSERT_TRUE(0x0807060504030201ULL == result);
    TEST_ASSERT_EQUAL_INT(9, offset);
}

TEST_CASE("Varint decode truncated buffer bounds checks", "[coinbase_decoder]")
{
    int offset = 0;
    // NULL checks
    TEST_ASSERT_TRUE(0 == coinbase_decode_varint(NULL, 10, &offset));

    // Truncated FD (only 1 byte payload instead of 2)
    uint8_t trunc_fd[] = {0xFD, 0x34};
    offset = 0;
    TEST_ASSERT_TRUE(0 == coinbase_decode_varint(trunc_fd, sizeof(trunc_fd), &offset));

    // Truncated FE (only 2 bytes payload instead of 4)
    uint8_t trunc_fe[] = {0xFE, 0x78, 0x56};
    offset = 0;
    TEST_ASSERT_TRUE(0 == coinbase_decode_varint(trunc_fe, sizeof(trunc_fe), &offset));

    // Truncated FF (only 4 bytes payload instead of 8)
    uint8_t trunc_ff[] = {0xFF, 0x01, 0x02, 0x03, 0x04};
    offset = 0;
    TEST_ASSERT_TRUE(0 == coinbase_decode_varint(trunc_ff, sizeof(trunc_ff), &offset));

    // Offset at or beyond buffer length
    uint8_t single[] = {0x42};
    offset = 1;
    TEST_ASSERT_TRUE(0 == coinbase_decode_varint(single, sizeof(single), &offset));
}

TEST_CASE("Decode P2PKH address", "[coinbase_decoder]")
{
    // P2PKH: OP_DUP OP_HASH160 <20 bytes> OP_EQUALVERIFY OP_CHECKSIG
    uint8_t script[] = {
        0x76, 0xa9, 0x14,
        0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab,
        0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0x88, 0xac
    };
    char output[MAX_ADDRESS_STRING_LEN];
    
    coinbase_decode_address_from_scriptpubkey(script, sizeof(script), output, sizeof(output), "bc", false);
    
    TEST_ASSERT_EQUAL_STRING("1DYwPTnC4NgEmoqbLbcRqoSzVeH3ehmGbV", output);
}

TEST_CASE("Decode P2SH address", "[coinbase_decoder]")
{
    // P2SH: OP_HASH160 <20 bytes> OP_EQUAL
    uint8_t script[] = {
        0xa9, 0x14,
        0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34,
        0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78,
        0x87
    };
    char output[MAX_ADDRESS_STRING_LEN];
    
    coinbase_decode_address_from_scriptpubkey(script, sizeof(script), output, sizeof(output), "bc", false);
    
    TEST_ASSERT_EQUAL_STRING("33MGnVL6rnKqt6Jjt3HbRqWJrhwy65dMhS", output);
}

TEST_CASE("Decode P2WPKH address", "[coinbase_decoder]")
{
    // P2WPKH: OP_0 <20 bytes>
    uint8_t script[] = {
        0x00, 0x14,
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11, 0x22, 0x33,
        0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd
    };
    char output[MAX_ADDRESS_STRING_LEN];
    
    coinbase_decode_address_from_scriptpubkey(script, sizeof(script), output, sizeof(output), "bc", false);
    
    TEST_ASSERT_EQUAL_STRING("bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x", output);
}

TEST_CASE("Decode P2WSH address", "[coinbase_decoder]")
{
    // P2WSH: OP_0 <32 bytes>
    uint8_t script[] = {
        0x00, 0x20,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
        0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
        0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
    };
    char output[MAX_ADDRESS_STRING_LEN];
    
    coinbase_decode_address_from_scriptpubkey(script, sizeof(script), output, sizeof(output), "bc", false);
    
    TEST_ASSERT_EQUAL_STRING("bc1qqypqxpq9qcrsszg2pvxq6rs0zqg3yyc5z5tpwxqergd3c8g7rusqyp0mu0", output);
}

TEST_CASE("Decode P2TR address", "[coinbase_decoder]")
{
    // P2TR: OP_1 <32 bytes>
    uint8_t script[] = {
        0x51, 0x20,
        0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88,
        0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00,
        0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88,
        0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00
    };
    char output[MAX_ADDRESS_STRING_LEN];
    
    coinbase_decode_address_from_scriptpubkey(script, sizeof(script), output, sizeof(output), "bc", false);
    
    TEST_ASSERT_EQUAL_STRING("bc1pllhdmn9m42vcsamx24zrxgs3qrl7ahwvhw4fnzrhve25gvezzyqqc0cgpt", output);
}

// Testnet address tests

TEST_CASE("Decode testnet P2PKH address", "[coinbase_decoder]")
{
    // Same hash as mainnet P2PKH test, but with testnet version byte (0x6F)
    uint8_t script[] = {
        0x76, 0xa9, 0x14,
        0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab,
        0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0x88, 0xac
    };
    char output[MAX_ADDRESS_STRING_LEN];

    coinbase_decode_address_from_scriptpubkey(script, sizeof(script), output, sizeof(output), "tb", true);

    // Testnet P2PKH addresses start with 'm' or 'n'
    TEST_ASSERT_TRUE(output[0] == 'm' || output[0] == 'n');
}

TEST_CASE("Decode testnet P2SH address", "[coinbase_decoder]")
{
    // Same hash as mainnet P2SH test, but with testnet version byte (0xC4)
    uint8_t script[] = {
        0xa9, 0x14,
        0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34,
        0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78,
        0x87
    };
    char output[MAX_ADDRESS_STRING_LEN];

    coinbase_decode_address_from_scriptpubkey(script, sizeof(script), output, sizeof(output), "tb", true);

    // Testnet P2SH addresses start with '2'
    TEST_ASSERT_EQUAL_CHAR('2', output[0]);
}

TEST_CASE("Decode testnet P2WPKH address", "[coinbase_decoder]")
{
    // Same hash as mainnet P2WPKH test, but with "tb" HRP
    uint8_t script[] = {
        0x00, 0x14,
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11, 0x22, 0x33,
        0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd
    };
    char output[MAX_ADDRESS_STRING_LEN];

    coinbase_decode_address_from_scriptpubkey(script, sizeof(script), output, sizeof(output), "tb", true);

    TEST_ASSERT_TRUE(strncmp(output, "tb1q", 4) == 0);
}

TEST_CASE("Decode testnet P2TR address", "[coinbase_decoder]")
{
    // Same hash as mainnet P2TR test, but with "tb" HRP
    uint8_t script[] = {
        0x51, 0x20,
        0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88,
        0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00,
        0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88,
        0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00
    };
    char output[MAX_ADDRESS_STRING_LEN];

    coinbase_decode_address_from_scriptpubkey(script, sizeof(script), output, sizeof(output), "tb", true);

    TEST_ASSERT_TRUE(strncmp(output, "tb1p", 4) == 0);
}

TEST_CASE("Decode regtest P2WPKH address", "[coinbase_decoder]")
{
    // Same hash as mainnet P2WPKH test, but with "bcrt" HRP
    uint8_t script[] = {
        0x00, 0x14,
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11, 0x22, 0x33,
        0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd
    };
    char output[MAX_ADDRESS_STRING_LEN];

    coinbase_decode_address_from_scriptpubkey(script, sizeof(script), output, sizeof(output), "bcrt", true);

    TEST_ASSERT_TRUE(strncmp(output, "bcrt1q", 6) == 0);
}

// Network auto-detection tests via coinbase_process_notification are
// integration-level — the detection logic is tested implicitly through
// the address prefix matching in the full processing pipeline.

static esp_err_t test_process_v1_job(const char *c1, const char *c2, uint32_t version, const char *extranonce1, int extranonce2_len, const char *user_address, bool decode_coinbase_tx, mining_notification_result_t *result) {
    miner_job_t job = { 0 };
    job.type = JOB_TYPE_V1;
    job.version = version;
    job.nbits = 0x1d00ffff;
    if (c1) {
        hex2bin(c1, job.coinbase_prefix, strlen(c1) / 2);
        job.coinbase_prefix_len = strlen(c1) / 2;
    }
    if (c2) {
        hex2bin(c2, job.coinbase_suffix, strlen(c2) / 2);
        job.coinbase_suffix_len = strlen(c2) / 2;
    }
    if (extranonce1) {
        job.extranonce1_len = strlen(extranonce1) / 2;
        hex2bin(extranonce1, job.extranonce1, job.extranonce1_len);
    }
    job.extranonce2_len = (uint8_t)extranonce2_len;
    return coinbase_process_miner_job(&job, user_address, decode_coinbase_tx, result);
}

TEST_CASE("BIP-110 signaling not detected", "[coinbase_decoder]")
{
    const char *c1 = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b03a5020cfabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000";
    const char *c2 = "41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000";
    
    mining_notification_result_t result = { 0 };
    esp_err_t err = test_process_v1_job(c1, c2, 0x20000000, "01020304", 8, "", true, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(result.bip110_signaling);
}

TEST_CASE("BIP-110 signaling detected", "[coinbase_decoder]")
{
    const char *c1 = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b03a5020cfabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000";
    const char *c2 = "41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000";
    
    mining_notification_result_t result = { 0 };
    esp_err_t err = test_process_v1_job(c1, c2, 0x20000010, "01020304", 8, "", true, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(result.bip110_signaling);
}

TEST_CASE("BIP-110 signaling last block", "[coinbase_decoder]")
{
    const char *c1 = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b031fbc0efabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000";
    const char *c2 = "41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000";
    
    mining_notification_result_t result = { 0 };
    esp_err_t err = test_process_v1_job(c1, c2, 0x20000010, "01020304", 8, "", true, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(965663, result.block_height);
    TEST_ASSERT_TRUE(result.bip110_signaling);
}

TEST_CASE("BIP-110 signaling expired", "[coinbase_decoder]")
{
    const char *c1 = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b0320bc0efabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000";
    const char *c2 = "41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000";
    
    mining_notification_result_t result = { 0 };
    esp_err_t err = test_process_v1_job(c1, c2, 0x20000010, "01020304", 8, "", true, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(965664, result.block_height);
    TEST_ASSERT_FALSE(result.bip110_signaling);
}

TEST_CASE("Decode via miner_job_t directly", "[coinbase_decoder]")
{
    miner_job_t job = { 0 };
    job.type = JOB_TYPE_V1;
    job.version = 0x20000000;
    job.nbits = 0x1d00ffff;
    const char *c1 = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b031fbc0efabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000";
    const char *c2 = "41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000";
    hex2bin(c1, job.coinbase_prefix, strlen(c1) / 2);
    job.coinbase_prefix_len = strlen(c1) / 2;
    hex2bin(c2, job.coinbase_suffix, strlen(c2) / 2);
    job.coinbase_suffix_len = strlen(c2) / 2;

    job.extranonce1_len = 4;
    hex2bin("01020304", job.extranonce1, 4);
    job.extranonce2_len = 8;

    mining_notification_result_t result = { 0 };
    esp_err_t err = coinbase_process_miner_job(&job, "", true, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(965663, result.block_height);

    // Test NULL user_address handling (PR hardening)
    mining_notification_result_t null_user_result = { 0 };
    esp_err_t err_null = coinbase_process_miner_job(&job, NULL, true, &null_user_result);
    TEST_ASSERT_EQUAL(ESP_OK, err_null);
    TEST_ASSERT_EQUAL(965663, null_user_result.block_height);
}