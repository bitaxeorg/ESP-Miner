#include "utils.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void assert_guards(const uint8_t buffer[48], size_t start)
{
    for (size_t i = 0; i < 48; ++i) {
        if (i < start || i >= start + 32) {
            assert(buffer[i] == 0xa5);
        }
    }
}

static void test_word_reversal(void)
{
    const uint8_t expected[32] = {
        28, 29, 30, 31, 24, 25, 26, 27, 20, 21, 22, 23, 16, 17, 18, 19,
        12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3,
    };
    for (size_t src_offset = 8; src_offset < 16; ++src_offset) {
        for (size_t dest_offset = 8; dest_offset < 16; ++dest_offset) {
            _Alignas(8) uint8_t src[48], dest[48];
            memset(src, 0xa5, sizeof(src));
            memset(dest, 0xa5, sizeof(dest));
            for (size_t i = 0; i < 32; ++i) {
                src[src_offset + i] = (uint8_t)i;
            }
            reverse_32bit_words(src + src_offset, dest + dest_offset);
            assert(memcmp(expected, dest + dest_offset, 32) == 0);
            for (size_t i = 0; i < 32; ++i) {
                assert(src[src_offset + i] == i);
            }
            assert_guards(src, src_offset);
            assert_guards(dest, dest_offset);
        }
    }
}

static void test_byte_reversal(void)
{
    const uint8_t expected[32] = {
        3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12,
        19, 18, 17, 16, 23, 22, 21, 20, 27, 26, 25, 24, 31, 30, 29, 28,
    };
    for (size_t offset = 8; offset < 16; ++offset) {
        _Alignas(8) uint8_t buffer[48];
        memset(buffer, 0xa5, sizeof(buffer));
        for (size_t i = 0; i < 32; ++i) {
            buffer[offset + i] = (uint8_t)i;
        }
        reverse_endianness_per_word(buffer + offset);
        assert(memcmp(expected, buffer + offset, 32) == 0);
        assert_guards(buffer, offset);
        reverse_endianness_per_word(buffer + offset);
        for (size_t i = 0; i < 32; ++i) {
            assert(buffer[offset + i] == i);
        }
        assert_guards(buffer, offset);
    }
}

static void test_target_conversion(void)
{
    for (size_t offset = 8; offset < 16; ++offset) {
        _Alignas(8) uint8_t buffer[48];
        memset(buffer, 0xa5, sizeof(buffer));
        memset(buffer + offset, 0, 32);
        assert(le256todouble(buffer + offset) == 0.0);
        for (unsigned bit = 0; bit < 256; ++bit) {
            memset(buffer + offset, 0, 32);
            buffer[offset + bit / 8] = (uint8_t)(1u << (bit % 8));
            assert(le256todouble(buffer + offset) == ldexp(1.0, bit));
        }
        memset(buffer + offset, 0xff, 32);
        assert(le256todouble(buffer + offset) == 0x1p256);

        /* Exact 53-bit mantissa, with bits in two different 64-bit limbs. */
        memset(buffer + offset, 0, 32);
        buffer[offset + 10] = 1;
        buffer[offset + 3] = 0x10;
        assert(le256todouble(buffer + offset) == 0x1.0000000000001p80);
        assert_guards(buffer, offset);
    }
}

int main(void)
{
    test_word_reversal();
    test_byte_reversal();
    test_target_conversion();
    puts("Hash byte tests passed: all source/destination alignments and 256 target bits.");
    return 0;
}
