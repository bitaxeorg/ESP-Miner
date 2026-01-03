#include "unity.h"
#include "utils.h"
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
    hex2bin(hex_string, bin, bin_len);
    TEST_ASSERT_EQUAL(72, bin[0]);
    TEST_ASSERT_EQUAL(69, bin[1]);
    TEST_ASSERT_EQUAL(76, bin[2]);
    TEST_ASSERT_EQUAL(76, bin[3]);
    TEST_ASSERT_EQUAL(79, bin[4]);
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
