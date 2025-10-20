#include "unity.h"
#include "utils.h"
#include <string.h>

TEST_CASE("Test double sha", "[utils]")
{
    const char *input = "68656c6c6f";
    char *output = double_sha256(input);
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
    bin2hex(bin, 5, hex_string, 11);
    TEST_ASSERT_EQUAL_STRING("48454c4c4f", hex_string);
}
