#include "unity.h"
#include "utils.h"
#include <string.h>

TEST_CASE("Test sha256_bin", "[utils]")
{
    const char input[] = "hello";
    uint8_t hash[32];
    TEST_ASSERT_TRUE(sha256_bin((const uint8_t *)input, 5, hash));
    char output[65];
    bin2hex(hash, 32, output, 65);
    TEST_ASSERT_EQUAL_STRING("2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824", output);
}

TEST_CASE("Test double_sha256_bin", "[utils]")
{
    const char input[] = "hello";
    uint8_t hash[32];
    TEST_ASSERT_TRUE(double_sha256_bin((uint8_t *)input, 5, hash));
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
    const uint8_t expected[32] = {28, 29, 30, 31,
                            24, 25, 26, 27,
                            20, 21, 22, 23,
                            16, 17, 18, 19,
                            12, 13, 14, 15,
                             8,  9, 10, 11,
                             4,  5,  6,  7,
                             0,  1,  2,  3};
    for (size_t source_offset = 8; source_offset < 16; source_offset++) {
        for (size_t destination_offset = 8; destination_offset < 16; destination_offset++) {
            _Alignas(8) uint8_t source[48], destination[48];
            uint8_t original_source[sizeof(source)];
            uint8_t expected_destination[sizeof(destination)];
            memset(source, 0xa5, sizeof(source));
            memset(destination, 0xa5, sizeof(destination));
            memset(expected_destination, 0xa5, sizeof(expected_destination));
            for (size_t i = 0; i < 32; i++) source[source_offset + i] = (uint8_t)i;
            memcpy(original_source, source, sizeof(source));
            memcpy(expected_destination + destination_offset, expected, sizeof(expected));

            reverse_32bit_words(source + source_offset, destination + destination_offset);

            /* Compare the whole buffers, including the surrounding guard bytes. */
            TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_destination, destination, sizeof(destination));
            TEST_ASSERT_EQUAL_HEX8_ARRAY(original_source, source, sizeof(source));
        }
    }
}

TEST_CASE("word reversal supports overlapping buffers", "[utils]")
{
    const uint8_t expected_words[32] = {
        28, 29, 30, 31, 24, 25, 26, 27, 20, 21, 22, 23, 16, 17, 18, 19,
        12, 13, 14, 15,  8,  9, 10, 11,  4,  5,  6,  7,  0,  1,  2,  3
    };
    const size_t offsets[][2] = {
        {0, 0}, {0, 4}, {4, 0}, {1, 1}, {1, 5}, {5, 1}, {0, 1}, {1, 0}
    };

    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
        _Alignas(uint32_t) uint8_t storage[48];
        uint8_t expected[sizeof(storage)];
        memset(storage, 0xa5, sizeof(storage));
        uint8_t *source = storage + 4 + offsets[i][0];
        uint8_t *destination = storage + 4 + offsets[i][1];
        for (int byte = 0; byte < 32; byte++) source[byte] = byte;
        memcpy(expected, storage, sizeof(expected));
        memcpy(expected + 4 + offsets[i][1], expected_words, sizeof(expected_words));

        reverse_32bit_words(source, destination);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, storage, sizeof(storage));
    }
}

TEST_CASE("reverse_endianness_per_word", "[utils]")
{
    const uint8_t expected[32] = { 3,  2,  1,  0,
                             7,  6,  5,  4,
                            11, 10,  9,  8,
                            15, 14, 13, 12,
                            19, 18, 17, 16,
                            23, 22, 21, 20,
                            27, 26, 25, 24,
                            31, 30, 29, 28};
    for (size_t offset = 8; offset < 16; offset++) {
        _Alignas(8) uint8_t storage[48];
        uint8_t original[sizeof(storage)], expected_storage[sizeof(storage)];
        memset(storage, 0xa5, sizeof(storage));
        memset(expected_storage, 0xa5, sizeof(expected_storage));
        for (size_t i = 0; i < 32; i++) storage[offset + i] = (uint8_t)i;
        memcpy(original, storage, sizeof(storage));
        memcpy(expected_storage + offset, expected, sizeof(expected));

        reverse_endianness_per_word(storage + offset);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_storage, storage, sizeof(storage));

        reverse_endianness_per_word(storage + offset);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(original, storage, sizeof(storage));
    }
}

TEST_CASE("target_to_diff safety", "[utils]")
{
    // 1. NULL pointer
    TEST_ASSERT_EQUAL_DOUBLE((double)UINT32_MAX, target_to_diff(NULL));

    // 2. All zero target (division by zero guard)
    uint8_t zero_target[32] = {0};
    TEST_ASSERT_EQUAL_DOUBLE((double)UINT32_MAX, target_to_diff(zero_target));

    // 3. Max difficulty 1 target (0x00000000ffff0000...00)
    uint8_t diff1_target[32] = {0};
    diff1_target[26] = 0xff;
    diff1_target[27] = 0xff;
    double d1 = target_to_diff(diff1_target);
    TEST_ASSERT_TRUE(d1 >= 0.99 && d1 <= 1.01);
}

TEST_CASE("nbits_to_target", "[utils]")
{
    uint8_t target[32];

    // Genesis / Diff 1 nBits: 0x1d00ffff
    nbits_to_target(0x1d00ffff, target);
    uint8_t expected_genesis[32] = {0};
    expected_genesis[26] = 0xff;
    expected_genesis[27] = 0xff;
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_genesis, target, 32);

    // Test conversion back to difficulty via target_to_diff
    double diff = target_to_diff(target);
    TEST_ASSERT_TRUE(diff >= 0.9999 && diff <= 1.0001);

    // Mainnet block nBits (0x1701cdfb)
    nbits_to_target(0x1701cdfb, target);
    double actual_diff = target_to_diff(target);
    double expected_diff = 155973032196071.9;
    TEST_ASSERT_FLOAT_WITHIN(expected_diff * 0.0001, expected_diff, actual_diff);

    // Negative / overflow nBits (bit 23 set)
    nbits_to_target(0x1d80ffff, target);
    uint8_t zero_target[32] = {0};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(zero_target, target, 32);
}

TEST_CASE("diff_to_target", "[utils]")
{
    uint8_t target[32];

    // diff 1.0 matches diff 1 target
    diff_to_target(1.0, target);
    uint8_t expected_diff1[32] = {0};
    expected_diff1[26] = 0xff;
    expected_diff1[27] = 0xff;
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_diff1, target, 32);
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 1.0, target_to_diff(target));

    // diff 1000.0 round-trip
    diff_to_target(1000.0, target);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 1000.0, target_to_diff(target));

    // diff 0.5 round-trip
    diff_to_target(0.5, target);
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 0.5, target_to_diff(target));

    // diff <= 0.0 returns all-zero target
    diff_to_target(0.0, target);
    uint8_t zero_target[32] = {0};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(zero_target, target, 32);

    diff_to_target(-5.0, target);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(zero_target, target, 32);
}

TEST_CASE("uint256_lte boundary conditions", "[utils]")
{
    uint8_t target[32] = {0};
    uint8_t hash[32] = {0};

    // Equal (all zeros) -> true
    TEST_ASSERT_TRUE(uint256_lte(hash, target));

    // Target set to diff 1 (byte 26 = 0xff, byte 27 = 0xff)
    target[26] = 0xff;
    target[27] = 0xff;

    // hash < target (hash is 0) -> true
    TEST_ASSERT_TRUE(uint256_lte(hash, target));

    // hash == target -> true
    memcpy(hash, target, 32);
    TEST_ASSERT_TRUE(uint256_lte(hash, target));

    // hash > target by 1 in highest word -> false
    hash[27] = 0xff;
    hash[28] = 0x01;
    TEST_ASSERT_FALSE(uint256_lte(hash, target));

    // hash > target by 1 in lowest byte -> false
    memcpy(hash, target, 32);
    hash[0] = 0x01;
    TEST_ASSERT_FALSE(uint256_lte(hash, target));

    // hash < target by 1 in lowest byte -> true
    memset(hash, 0, 32);
    hash[26] = 0xff;
    hash[27] = 0xff;
    memset(target, 0, 32);
    target[26] = 0xff;
    target[27] = 0xff;
    target[0] = 0x02;
    hash[0] = 0x01;
    TEST_ASSERT_TRUE(uint256_lte(hash, target));
}
