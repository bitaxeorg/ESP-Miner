#include "unity.h"
#include "utils.h"
#include "mining.h"
#include <string.h>

TEST_CASE("Test double_sha256_bin", "[utils]")
{
    const char input[] = "hello";
    uint8_t hash[32];
    double_sha256_bin((uint8_t *)input, 5, hash);
    char output[65];
    bin2hex(hash, 32, output, 65);
    TEST_ASSERT_EQUAL_STRING("9595c9df90075148eb06860365df33584b75bff782a510c6cd4883a419833d50", output);
}

TEST_CASE("Test hex2bin", "[utils]")
{
    char *hex_string = "48454c4c4f";
    size_t bin_len = strlen(hex_string) / 2;
    uint8_t *bin = malloc(bin_len);
    TEST_ASSERT_NOT_NULL(bin);
    TEST_ASSERT_EQUAL(bin_len, hex2bin(hex_string, bin, bin_len));
    TEST_ASSERT_EQUAL(72, bin[0]);
    TEST_ASSERT_EQUAL(69, bin[1]);
    TEST_ASSERT_EQUAL(76, bin[2]);
    TEST_ASSERT_EQUAL(76, bin[3]);
    TEST_ASSERT_EQUAL(79, bin[4]);
    free(bin);

    uint8_t buf[4];
    TEST_ASSERT_EQUAL(0, hex2bin(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(0, hex2bin("48", NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL(0, hex2bin("484", buf, sizeof(buf)));     // Odd length
    TEST_ASSERT_EQUAL(0, hex2bin("484z", buf, sizeof(buf)));    // Invalid character 'z'
    TEST_ASSERT_EQUAL(0, hex2bin("48\x80", buf, sizeof(buf)));  // High byte
}

TEST_CASE("Test hex_val_table and hex_decode_byte", "[utils]")
{
    TEST_ASSERT_EQUAL(0, hex_val_table[(unsigned char)'0']);
    TEST_ASSERT_EQUAL(9, hex_val_table[(unsigned char)'9']);
    TEST_ASSERT_EQUAL(10, hex_val_table[(unsigned char)'a']);
    TEST_ASSERT_EQUAL(15, hex_val_table[(unsigned char)'f']);
    TEST_ASSERT_EQUAL(10, hex_val_table[(unsigned char)'A']);
    TEST_ASSERT_EQUAL(15, hex_val_table[(unsigned char)'F']);
    TEST_ASSERT_EQUAL(-1, hex_val_table[(unsigned char)'g']);
    TEST_ASSERT_EQUAL(-1, hex_val_table[(unsigned char)'G']);
    TEST_ASSERT_EQUAL(-1, hex_val_table[(unsigned char)'/']);
    TEST_ASSERT_EQUAL(-1, hex_val_table[(unsigned char)':']);
    TEST_ASSERT_EQUAL(-1, hex_val_table[(unsigned char)'\0']);
    TEST_ASSERT_EQUAL(-1, hex_val_table[(unsigned char)0x80]);
    TEST_ASSERT_EQUAL(-1, hex_val_table[(unsigned char)0xFF]);

    TEST_ASSERT_EQUAL(0x00, hex_decode_byte("00"));
    TEST_ASSERT_EQUAL(0x48, hex_decode_byte("48"));
    TEST_ASSERT_EQUAL(0xFF, hex_decode_byte("FF"));
    TEST_ASSERT_EQUAL(0xab, hex_decode_byte("ab"));
    TEST_ASSERT_EQUAL(-1, hex_decode_byte("4"));
    TEST_ASSERT_EQUAL(-1, hex_decode_byte("4z"));
    TEST_ASSERT_EQUAL(-1, hex_decode_byte("z4"));
}

TEST_CASE("Test url_decode", "[utils]")
{
    char decoded[64];

    url_decode(decoded, "hello+world");
    TEST_ASSERT_EQUAL_STRING("hello world", decoded);

    url_decode(decoded, "hello%20world");
    TEST_ASSERT_EQUAL_STRING("hello world", decoded);

    url_decode(decoded, "hello%2Fworld%21");
    TEST_ASSERT_EQUAL_STRING("hello/world!", decoded);

    url_decode(decoded, "100%25");
    TEST_ASSERT_EQUAL_STRING("100%", decoded);

    // Invalid escape sequences preserved gracefully
    url_decode(decoded, "100%");
    TEST_ASSERT_EQUAL_STRING("100%", decoded);

    url_decode(decoded, "100%2");
    TEST_ASSERT_EQUAL_STRING("100%2", decoded);

    url_decode(decoded, "100%zz");
    TEST_ASSERT_EQUAL_STRING("100%zz", decoded);
}

TEST_CASE("Test bin2hex", "[utils]")
{
    uint8_t bin[5] = {72, 69, 76, 76, 79};
    char hex_string[11];
    TEST_ASSERT_EQUAL(0, bin2hex(bin, 5, hex_string, 10));
    TEST_ASSERT_EQUAL(10, bin2hex(bin, 5, hex_string, 11));
    TEST_ASSERT_EQUAL_STRING("48454c4c4f", hex_string);
}

TEST_CASE("reverse_32bit_words", "[utils]")
{
    uint8_t input[32];
    for (int i = 0; i < 32; i++) input[i] = i;

    uint8_t actual[32];
    reverse_32bit_words(input, actual);

    uint8_t expected[32] = {28, 29, 30, 31,
                            24, 25, 26, 27,
                            20, 21, 22, 23,
                            16, 17, 18, 19,
                            12, 13, 14, 15,
                             8,  9, 10, 11,
                             4,  5,  6,  7,
                             0,  1,  2,  3};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, 32);
}

TEST_CASE("reverse_endianness_per_word", "[utils]")
{
    uint8_t data[32];
    for (int i = 0; i < 32; i++) data[i] = i;

    reverse_endianness_per_word(data);

    uint8_t expected[32] = { 3,  2,  1,  0,
                             7,  6,  5,  4,
                            11, 10,  9,  8,
                            15, 14, 13, 12,
                            19, 18, 17, 16,
                            23, 22, 21, 20,
                            27, 26, 25, 24,
                            31, 30, 29, 28};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, data, 32);
}

TEST_CASE("networkDifficulty", "[utils]")
{
    uint32_t nBits = 0x1701cdfb;

    double actual = networkDifficulty(nBits);

    double expected = 155973032196071.9;

    TEST_ASSERT_EQUAL_DOUBLE(expected, actual);
}

TEST_CASE("hash_to_pdiff safety", "[mining]")
{
    // 1. NULL pointer
    TEST_ASSERT_EQUAL_DOUBLE((double)UINT32_MAX, hash_to_pdiff(NULL));

    // 2. All zero target (division by zero guard)
    uint8_t zero_target[32] = {0};
    TEST_ASSERT_EQUAL_DOUBLE((double)UINT32_MAX, hash_to_pdiff(zero_target));

    // 3. Max difficulty 1 target (0x00000000ffff0000...00)
    uint8_t diff1_target[32] = {0};
    diff1_target[26] = 0xff;
    diff1_target[27] = 0xff;
    double d1 = hash_to_pdiff(diff1_target);
    TEST_ASSERT_TRUE(d1 >= 0.99 && d1 <= 1.01);
}
