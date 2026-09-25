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

static uint8_t s_test_pbuf[1024];
static uint8_t s_test_sbuf[2048];

static esp_err_t test_process_v1_job(const char *c1, const char *c2, uint32_t version, const char *extranonce1, int extranonce2_len, const char *user_address, bool decode_coinbase_tx, mining_notification_result_t *result) {
    static miner_job_t job;
    memset(&job, 0, sizeof(job));
    job.coinbase_prefix = s_test_pbuf;
    job.coinbase_suffix = s_test_sbuf;
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
    // This captured template reserves 15 scriptSig bytes for extranonces (7 bytes e1 + 8 bytes e2).
    esp_err_t err = test_process_v1_job(c1, c2, 0x20000000, "01020304050607", 8, "", true, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_INT(3, result.output_count);
    TEST_ASSERT_FALSE(result.bip110_signaling);
    if (result.scriptsig) free(result.scriptsig);
}

TEST_CASE("BIP-110 signaling detected", "[coinbase_decoder]")
{
    const char *c1 = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b03a5020cfabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000";
    const char *c2 = "41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000";
    
    mining_notification_result_t result = { 0 };
    // This captured template reserves 15 scriptSig bytes for extranonces (7 bytes e1 + 8 bytes e2).
    esp_err_t err = test_process_v1_job(c1, c2, 0x20000010, "01020304050607", 8, "", true, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_INT(3, result.output_count);
    TEST_ASSERT_TRUE(result.bip110_signaling);
    if (result.scriptsig) free(result.scriptsig);
}

TEST_CASE("BIP-110 signaling last block", "[coinbase_decoder]")
{
    const char *c1 = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b031fbc0efabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000";
    const char *c2 = "41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000";
    
    mining_notification_result_t result = { 0 };
    // This captured template reserves 15 scriptSig bytes for extranonces (7 bytes e1 + 8 bytes e2).
    esp_err_t err = test_process_v1_job(c1, c2, 0x20000010, "01020304050607", 8, "", true, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_INT(3, result.output_count);
    TEST_ASSERT_EQUAL(965663, result.block_height);
    TEST_ASSERT_TRUE(result.bip110_signaling);
    if (result.scriptsig) free(result.scriptsig);
}

TEST_CASE("BIP-110 signaling expired", "[coinbase_decoder]")
{
    const char *c1 = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b0320bc0efabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000";
    const char *c2 = "41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000";
    
    mining_notification_result_t result = { 0 };
    // This captured template reserves 15 scriptSig bytes for extranonces (7 bytes e1 + 8 bytes e2).
    esp_err_t err = test_process_v1_job(c1, c2, 0x20000010, "01020304050607", 8, "", true, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_INT(3, result.output_count);
    TEST_ASSERT_EQUAL(965664, result.block_height);
    TEST_ASSERT_FALSE(result.bip110_signaling);
    if (result.scriptsig) free(result.scriptsig);
}

TEST_CASE("Decode via miner_job_t directly", "[coinbase_decoder]")
{
    static miner_job_t job;
    memset(&job, 0, sizeof(job));
    job.coinbase_prefix = s_test_pbuf;
    job.coinbase_suffix = s_test_sbuf;
    job.type = JOB_TYPE_V1;
    job.version = 0x20000000;
    job.nbits = 0x1d00ffff;
    const char *c1 = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b031fbc0efabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000";
    const char *c2 = "41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000";
    hex2bin(c1, job.coinbase_prefix, strlen(c1) / 2);
    job.coinbase_prefix_len = strlen(c1) / 2;
    hex2bin(c2, job.coinbase_suffix, strlen(c2) / 2);
    job.coinbase_suffix_len = strlen(c2) / 2;

    job.extranonce1_len = 7;
    hex2bin("01020304050607", job.extranonce1, 7);
    job.extranonce2_len = 8;

    mining_notification_result_t result = { 0 };
    esp_err_t err = coinbase_process_miner_job(&job, "", true, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_INT(3, result.output_count);
    TEST_ASSERT_EQUAL(965663, result.block_height);
    if (result.scriptsig) free(result.scriptsig);

    // Test NULL user_address handling (PR hardening)
    mining_notification_result_t null_user_result = { 0 };
    esp_err_t err_null = coinbase_process_miner_job(&job, NULL, true, &null_user_result);
    TEST_ASSERT_EQUAL(ESP_OK, err_null);
    TEST_ASSERT_EQUAL_INT(3, null_user_result.output_count);
    TEST_ASSERT_EQUAL(965663, null_user_result.block_height);
    if (null_user_result.scriptsig) free(null_user_result.scriptsig);
}

TEST_CASE("Coinbase decoder requires exactly one locktime", "[coinbase_decoder][security]")
{
    static miner_job_t job;
    memset(&job, 0, sizeof(job));
    job.coinbase_prefix = s_test_pbuf;
    job.coinbase_suffix = s_test_sbuf;
    job.type = JOB_TYPE_V1;
    job.version = 0x20000000;
    job.nbits = 0x1d00ffff;
    const char *c1 = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b031fbc0efabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000";
    const char *c2 = "41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000";
    hex2bin(c1, job.coinbase_prefix, strlen(c1) / 2);
    job.coinbase_prefix_len = strlen(c1) / 2;
    hex2bin(c2, job.coinbase_suffix, strlen(c2) / 2);
    job.coinbase_suffix_len = strlen(c2) / 2;
    job.extranonce1_len = 7;
    hex2bin("01020304050607", job.extranonce1, 7);
    job.extranonce2_len = 8;

    mining_notification_result_t result = { 0 };

    // Valid job succeeds
    TEST_ASSERT_EQUAL(ESP_OK, coinbase_process_miner_job(&job, "", true, &result));
    TEST_ASSERT_EQUAL_INT(3, result.output_count);
    if (result.scriptsig) free(result.scriptsig);

    // Missing locktime bytes (truncate suffix by 4 bytes)
    job.coinbase_suffix_len -= 4;
    memset(&result, 0, sizeof(result));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, coinbase_process_miner_job(&job, "", true, &result));
    TEST_ASSERT_NULL(result.scriptsig);

    // Extra trailing bytes after locktime
    job.coinbase_suffix_len = strlen(c2) / 2 + 1; // original + 1 byte
    memset(&result, 0, sizeof(result));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, coinbase_process_miner_job(&job, "", true, &result));
    TEST_ASSERT_NULL(result.scriptsig);
}

TEST_CASE("Address to scriptpubkey - Bech32 P2WPKH mainnet and testnet round-trip", "[coinbase_decoder]")
{
    // Script: OP_0 OP_PUSHDATA_20 <20 bytes>
    uint8_t script[22] = {
        0x00, 0x14,
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11, 0x22, 0x33,
        0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd
    };
    char addr_mainnet[MAX_ADDRESS_STRING_LEN];
    char addr_testnet[MAX_ADDRESS_STRING_LEN];
    char addr_regtest[MAX_ADDRESS_STRING_LEN];

    coinbase_decode_address_from_scriptpubkey(script, sizeof(script), addr_mainnet, sizeof(addr_mainnet), "bc", false);
    coinbase_decode_address_from_scriptpubkey(script, sizeof(script), addr_testnet, sizeof(addr_testnet), "tb", true);
    coinbase_decode_address_from_scriptpubkey(script, sizeof(script), addr_regtest, sizeof(addr_regtest), "bcrt", true);

    uint8_t out[MAX_SCRIPTPUBKEY_LEN];
    size_t out_len = 0;

    // Mainnet
    out_len = coinbase_address_to_scriptpubkey(addr_mainnet, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(22, out_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(script, out, 22);

    // Testnet
    out_len = coinbase_address_to_scriptpubkey(addr_testnet, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(22, out_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(script, out, 22);

    // Regtest
    out_len = coinbase_address_to_scriptpubkey(addr_regtest, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(22, out_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(script, out, 22);
}

TEST_CASE("Address to scriptpubkey - Taproot P2TR mainnet and testnet round-trip", "[coinbase_decoder]")
{
    // Script: OP_1 OP_PUSHDATA_32 <32 bytes>
    uint8_t script[34] = {
        0x51, 0x20,
        0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88,
        0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00,
        0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88,
        0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00
    };
    char addr_mainnet[MAX_ADDRESS_STRING_LEN];
    char addr_testnet[MAX_ADDRESS_STRING_LEN];

    coinbase_decode_address_from_scriptpubkey(script, sizeof(script), addr_mainnet, sizeof(addr_mainnet), "bc", false);
    coinbase_decode_address_from_scriptpubkey(script, sizeof(script), addr_testnet, sizeof(addr_testnet), "tb", true);

    uint8_t out[MAX_SCRIPTPUBKEY_LEN];
    size_t out_len = 0;

    out_len = coinbase_address_to_scriptpubkey(addr_mainnet, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(34, out_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(script, out, 34);

    out_len = coinbase_address_to_scriptpubkey(addr_testnet, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(34, out_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(script, out, 34);
}

TEST_CASE("Address to scriptpubkey - Base58 P2PKH and P2SH round-trip", "[coinbase_decoder]")
{
    // P2PKH: OP_DUP OP_HASH160 0x14 <20 bytes> OP_EQUALVERIFY OP_CHECKSIG
    uint8_t p2pkh_script[25] = {
        0x76, 0xa9, 0x14,
        0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab,
        0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0x88, 0xac
    };
    char p2pkh_main[MAX_ADDRESS_STRING_LEN];
    char p2pkh_test[MAX_ADDRESS_STRING_LEN];

    coinbase_decode_address_from_scriptpubkey(p2pkh_script, sizeof(p2pkh_script), p2pkh_main, sizeof(p2pkh_main), "bc", false);
    coinbase_decode_address_from_scriptpubkey(p2pkh_script, sizeof(p2pkh_script), p2pkh_test, sizeof(p2pkh_test), "tb", true);

    uint8_t out[MAX_SCRIPTPUBKEY_LEN];
    size_t out_len = 0;

    out_len = coinbase_address_to_scriptpubkey(p2pkh_main, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(25, out_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(p2pkh_script, out, 25);

    out_len = coinbase_address_to_scriptpubkey(p2pkh_test, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(25, out_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(p2pkh_script, out, 25);

    // P2SH: OP_HASH160 0x14 <20 bytes> OP_EQUAL
    uint8_t p2sh_script[23] = {
        0xa9, 0x14,
        0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34,
        0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78,
        0x87
    };
    char p2sh_main[MAX_ADDRESS_STRING_LEN];
    char p2sh_test[MAX_ADDRESS_STRING_LEN];

    coinbase_decode_address_from_scriptpubkey(p2sh_script, sizeof(p2sh_script), p2sh_main, sizeof(p2sh_main), "bc", false);
    coinbase_decode_address_from_scriptpubkey(p2sh_script, sizeof(p2sh_script), p2sh_test, sizeof(p2sh_test), "tb", true);

    out_len = coinbase_address_to_scriptpubkey(p2sh_main, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(23, out_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(p2sh_script, out, 23);

    out_len = coinbase_address_to_scriptpubkey(p2sh_test, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(23, out_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(p2sh_script, out, 23);
}

TEST_CASE("Address to scriptpubkey - Worker and diff delimiters", "[coinbase_decoder]")
{
    const char *base = "bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x";
    uint8_t expected[MAX_SCRIPTPUBKEY_LEN];
    size_t exp_len = coinbase_address_to_scriptpubkey(base, expected, sizeof(expected));
    TEST_ASSERT_EQUAL_INT(22, exp_len);

    const char *candidates[] = {
        "bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x.worker1",
        "bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x_miner_01",
        "bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x/rig4",
        "bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x+d=1024",
        "bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x:password",
        "  bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x.axe1   "
    };

    uint8_t out[MAX_SCRIPTPUBKEY_LEN];
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        size_t len = coinbase_address_to_scriptpubkey(candidates[i], out, sizeof(out));
        TEST_ASSERT_EQUAL_INT(22, len);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 22);
    }
}

TEST_CASE("Address to scriptpubkey - Raw hex scriptPubKey support", "[coinbase_decoder]")
{
    const char *hex_wpkh = "0014aabbccddeeff00112233445566778899aabbccdd";
    uint8_t out[MAX_SCRIPTPUBKEY_LEN];
    size_t len = coinbase_address_to_scriptpubkey(hex_wpkh, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(22, len);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x14, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0xaa, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0xdd, out[21]);

    const char *hex_tr = "5120ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100";
    len = coinbase_address_to_scriptpubkey(hex_tr, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(34, len);
    TEST_ASSERT_EQUAL_HEX8(0x51, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x20, out[1]);
}

TEST_CASE("Address to scriptpubkey - Rejects account pool names and invalid inputs", "[coinbase_decoder]")
{
    uint8_t out[MAX_SCRIPTPUBKEY_LEN];

    // Account-based pool names
    TEST_ASSERT_EQUAL_INT(0, coinbase_address_to_scriptpubkey("antpool.worker1", out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(0, coinbase_address_to_scriptpubkey("braiins.rig1", out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(0, coinbase_address_to_scriptpubkey("foundry.worker", out, sizeof(out)));

    // Corrupted checksums
    TEST_ASSERT_EQUAL_INT(0, coinbase_address_to_scriptpubkey("bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y6250", out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(0, coinbase_address_to_scriptpubkey("1DYwPTnC4NgEmoqbLbcRqoSzVeH3ehmGb0", out, sizeof(out)));

    // Empty and NULL
    TEST_ASSERT_EQUAL_INT(0, coinbase_address_to_scriptpubkey("", out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(0, coinbase_address_to_scriptpubkey("   ", out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(0, coinbase_address_to_scriptpubkey(NULL, out, sizeof(out)));
}

TEST_CASE("Multi-address parsing up to 4 addresses", "[coinbase_decoder]")
{
    uint8_t scripts[MAX_USER_ADDRESSES][MAX_SCRIPTPUBKEY_LEN];
    size_t lens[MAX_USER_ADDRESSES];

    // 1 address
    int count = coinbase_parse_user_scriptpubkeys("bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x.worker1",
                                                  scripts, lens, MAX_USER_ADDRESSES);
    TEST_ASSERT_EQUAL_INT(1, count);
    TEST_ASSERT_EQUAL_INT(22, lens[0]);

    // 2 addresses (comma separated)
    count = coinbase_parse_user_scriptpubkeys("bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x.w1, 1DYwPTnC4NgEmoqbLbcRqoSzVeH3ehmGbV_w2",
                                             scripts, lens, MAX_USER_ADDRESSES);
    TEST_ASSERT_EQUAL_INT(2, count);
    TEST_ASSERT_EQUAL_INT(22, lens[0]);
    TEST_ASSERT_EQUAL_INT(25, lens[1]);

    // 4 addresses (mixed comma and semicolon)
    const char *four_addrs = "bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x; 1DYwPTnC4NgEmoqbLbcRqoSzVeH3ehmGbV, 33MGnVL6rnKqt6Jjt3HbRqWJrhwy65dMhS; bc1pllhdmn9m42vcsamx24zrxgs3qrl7ahwvhw4fnzrhve25gvezzyqqc0cgpt";
    count = coinbase_parse_user_scriptpubkeys(four_addrs, scripts, lens, MAX_USER_ADDRESSES);
    TEST_ASSERT_EQUAL_INT(4, count);
    TEST_ASSERT_EQUAL_INT(22, lens[0]);
    TEST_ASSERT_EQUAL_INT(25, lens[1]);
    TEST_ASSERT_EQUAL_INT(23, lens[2]);
    TEST_ASSERT_EQUAL_INT(34, lens[3]);

    // Clamping: when user provides 5 addresses but max_scripts is 4
    const char *five_addrs = "bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x, 1DYwPTnC4NgEmoqbLbcRqoSzVeH3ehmGbV, 33MGnVL6rnKqt6Jjt3HbRqWJrhwy65dMhS, bc1pllhdmn9m42vcsamx24zrxgs3qrl7ahwvhw4fnzrhve25gvezzyqqc0cgpt, bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x";
    count = coinbase_parse_user_scriptpubkeys(five_addrs, scripts, lens, MAX_USER_ADDRESSES);
    TEST_ASSERT_EQUAL_INT(4, count);
}

TEST_CASE("Ocean TIDES slot guarantee - user output beyond slot 5 displaces slot 5", "[coinbase_decoder]")
{
    // Build synthetic suffix with 8 outputs:
    // Outputs 0..6: 1000 sat each paying dummy P2SH (23 bytes script)
    // Output 7: 50000 sat paying user address (P2WPKH, 22 bytes)
    // Locktime: 00000000 (4 bytes)
    static miner_job_t job;
    memset(&job, 0, sizeof(job));
    job.coinbase_prefix = s_test_pbuf;
    job.coinbase_suffix = s_test_sbuf;
    job.type = JOB_TYPE_V1;
    job.version = 0x20000000;
    job.nbits = 0x1d00ffff;

    // Minimal prefix with height 100000 (0x0186a0), scriptsig_len=4, height_len=3 (remainder=0)
    const char *c1 = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0403a08601";
    hex2bin(c1, job.coinbase_prefix, strlen(c1) / 2);
    job.coinbase_prefix_len = strlen(c1) / 2;

    uint8_t user_script[22] = {
        0x00, 0x14,
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11, 0x22, 0x33,
        0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd
    };
    uint8_t other_script[23] = {
        0xa9, 0x14,
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00,
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00,
        0x87
    };

    uint8_t *s = s_test_sbuf;
    int pos = 0;

    // nSequence (4 bytes)
    s[pos++] = 0xff; s[pos++] = 0xff; s[pos++] = 0xff; s[pos++] = 0xff;

    // Output count: 8 outputs
    s[pos++] = 0x08;

    // Outputs 0..6: 1000 sat (0x03e8), 23 bytes other_script
    for (int i = 0; i < 7; i++) {
        uint64_t val = 1000;
        for (int b = 0; b < 8; b++) s[pos++] = (uint8_t)(val >> (b * 8));
        s[pos++] = 23; // script length
        memcpy(s + pos, other_script, 23);
        pos += 23;
    }

    // Output 7: 50000 sat (0xc350), 22 bytes user_script
    uint64_t user_val = 50000;
    for (int b = 0; b < 8; b++) s[pos++] = (uint8_t)(user_val >> (b * 8));
    s[pos++] = 22; // script length
    memcpy(s + pos, user_script, 22);
    pos += 22;

    // nLockTime: 4 bytes (0x00000000)
    s[pos++] = 0x00; s[pos++] = 0x00; s[pos++] = 0x00; s[pos++] = 0x00;

    job.coinbase_suffix_len = pos;
    job.extranonce1_len = 0;
    job.extranonce2_len = 0;

    mining_notification_result_t result = { 0 };
    const char *user_addr = "bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x.worker1";

    esp_err_t err = coinbase_process_miner_job(&job, user_addr, true, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_INT(6, result.output_count);
    TEST_ASSERT_TRUE(57000ULL == result.total_value_satoshis);
    TEST_ASSERT_TRUE(50000ULL == result.user_value_satoshis);

    // Slot 5 must be reserved/displaced for user output!
    TEST_ASSERT_TRUE(result.outputs[5].is_user_output);
    TEST_ASSERT_TRUE(50000ULL == result.outputs[5].value_satoshis);
    TEST_ASSERT_EQUAL_STRING("bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x", result.outputs[5].address);

    // Slots 0..4 must be other outputs
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_FALSE(result.outputs[i].is_user_output);
        TEST_ASSERT_TRUE(1000ULL == result.outputs[i].value_satoshis);
    }
}

TEST_CASE("Multi-address job payout verification", "[coinbase_decoder]")
{
    // Job with Output 0 paying user addr 1 (10,000 sat) and Output 1 paying user addr 2 (20,000 sat)
    static miner_job_t job;
    memset(&job, 0, sizeof(job));
    job.coinbase_prefix = s_test_pbuf;
    job.coinbase_suffix = s_test_sbuf;
    job.type = JOB_TYPE_V1;
    job.version = 0x20000000;
    job.nbits = 0x1d00ffff;

    // Minimal prefix with height 100000 (0x0186a0), scriptsig_len=4, height_len=3 (remainder=0)
    const char *c1 = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0403a08601";
    hex2bin(c1, job.coinbase_prefix, strlen(c1) / 2);
    job.coinbase_prefix_len = strlen(c1) / 2;

    uint8_t script1[22] = {
        0x00, 0x14,
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11, 0x22, 0x33,
        0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd
    };
    uint8_t script2[25] = {
        0x76, 0xa9, 0x14,
        0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab,
        0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0x88, 0xac
    };

    uint8_t *s = s_test_sbuf;
    int pos = 0;
    s[pos++] = 0xff; s[pos++] = 0xff; s[pos++] = 0xff; s[pos++] = 0xff; // nSequence
    s[pos++] = 0x02; // 2 outputs

    // Output 0: 10,000 sat
    uint64_t val1 = 10000;
    for (int b = 0; b < 8; b++) s[pos++] = (uint8_t)(val1 >> (b * 8));
    s[pos++] = 22;
    memcpy(s + pos, script1, 22);
    pos += 22;

    // Output 1: 20,000 sat
    uint64_t val2 = 20000;
    for (int b = 0; b < 8; b++) s[pos++] = (uint8_t)(val2 >> (b * 8));
    s[pos++] = 25;
    memcpy(s + pos, script2, 25);
    pos += 25;

    // nLockTime
    s[pos++] = 0x00; s[pos++] = 0x00; s[pos++] = 0x00; s[pos++] = 0x00;

    job.coinbase_suffix_len = pos;
    job.extranonce1_len = 0;
    job.extranonce2_len = 0;

    mining_notification_result_t result = { 0 };
    const char *multi_user = "bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x.miner1, 1DYwPTnC4NgEmoqbLbcRqoSzVeH3ehmGbV_miner2";

    esp_err_t err = coinbase_process_miner_job(&job, multi_user, true, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_INT(2, result.output_count);
    TEST_ASSERT_TRUE(30000ULL == result.total_value_satoshis);
    TEST_ASSERT_TRUE(30000ULL == result.user_value_satoshis);
    TEST_ASSERT_TRUE(result.outputs[0].is_user_output);
    TEST_ASSERT_TRUE(result.outputs[1].is_user_output);
    TEST_ASSERT_TRUE(result.has_user_address);
}

TEST_CASE("Account-based pool username - has_user_address is false", "[coinbase_decoder]")
{
    static miner_job_t job;
    memset(&job, 0, sizeof(job));
    job.coinbase_prefix = s_test_pbuf;
    job.coinbase_suffix = s_test_sbuf;
    job.type = JOB_TYPE_V1;
    job.version = 0x20000000;
    job.nbits = 0x1d00ffff;

    const char *c1 = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0403a08601";
    hex2bin(c1, job.coinbase_prefix, strlen(c1) / 2);
    job.coinbase_prefix_len = strlen(c1) / 2;

    uint8_t *s = s_test_sbuf;
    int pos = 0;
    s[pos++] = 0xff; s[pos++] = 0xff; s[pos++] = 0xff; s[pos++] = 0xff; // nSequence
    s[pos++] = 0x01; // 1 output

    uint64_t val = 50000;
    for (int b = 0; b < 8; b++) s[pos++] = (uint8_t)(val >> (b * 8));
    s[pos++] = 22;
    memset(s + pos, 0x11, 22);
    pos += 22;
    s[pos++] = 0x00; s[pos++] = 0x00; s[pos++] = 0x00; s[pos++] = 0x00; // locktime

    job.coinbase_suffix_len = pos;
    job.extranonce1_len = 0;
    job.extranonce2_len = 0;

    mining_notification_result_t result = { 0 };
    const char *account_user = "satoshi.worker1";

    esp_err_t err = coinbase_process_miner_job(&job, account_user, true, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(result.has_user_address);
    TEST_ASSERT_TRUE(0ULL == result.user_value_satoshis);
}

TEST_CASE("User scriptPubKey caching across jobs and cache clear", "[coinbase_decoder]")
{
    static miner_job_t job;
    memset(&job, 0, sizeof(job));
    job.coinbase_prefix = s_test_pbuf;
    job.coinbase_suffix = s_test_sbuf;
    job.type = JOB_TYPE_V1;
    job.version = 0x20000000;
    job.nbits = 0x1d00ffff;

    const char *c1 = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0403a08601";
    hex2bin(c1, job.coinbase_prefix, strlen(c1) / 2);
    job.coinbase_prefix_len = strlen(c1) / 2;

    uint8_t script[22] = {
        0x00, 0x14,
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11, 0x22, 0x33,
        0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd
    };

    uint8_t *s = s_test_sbuf;
    int pos = 0;
    s[pos++] = 0xff; s[pos++] = 0xff; s[pos++] = 0xff; s[pos++] = 0xff; // nSequence
    s[pos++] = 0x01; // 1 output

    uint64_t val = 50000;
    for (int b = 0; b < 8; b++) s[pos++] = (uint8_t)(val >> (b * 8));
    s[pos++] = 22;
    memcpy(s + pos, script, 22);
    pos += 22;
    s[pos++] = 0x00; s[pos++] = 0x00; s[pos++] = 0x00; s[pos++] = 0x00; // locktime

    job.coinbase_suffix_len = pos;
    job.extranonce1_len = 0;
    job.extranonce2_len = 0;

    mining_notification_result_t result = { 0 };
    const char *user = "bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x.worker1";

    // First call: populates cache
    esp_err_t err = coinbase_process_miner_job(&job, user, true, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(result.has_user_address);
    TEST_ASSERT_TRUE(50000ULL == result.user_value_satoshis);

    // Second call with same user: hits cache
    memset(&result, 0, sizeof(result));
    err = coinbase_process_miner_job(&job, user, true, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(result.has_user_address);
    TEST_ASSERT_TRUE(50000ULL == result.user_value_satoshis);

    // Clear cache and call again
    coinbase_clear_user_cache();
    memset(&result, 0, sizeof(result));
    err = coinbase_process_miner_job(&job, user, true, &result);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(result.has_user_address);
    TEST_ASSERT_TRUE(50000ULL == result.user_value_satoshis);
}