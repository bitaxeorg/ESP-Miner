#include "utils.h"
#include <string.h>

static inline void reverse_32bit_words_impl(const uint8_t src[32], uint8_t dest[32])
{
    uint32_t source[8];
    uint32_t reversed[8];

    // Built-in copies let GCC optimize aligned accesses despite ESP-IDF's
    // -fno-builtin-memcpy, while keeping byte-buffer accesses valid.
    __builtin_memcpy(source, src, sizeof(source));

    reversed[0] = source[7];
    reversed[1] = source[6];
    reversed[2] = source[5];
    reversed[3] = source[4];
    reversed[4] = source[3];
    reversed[5] = source[2];
    reversed[6] = source[1];
    reversed[7] = source[0];

    __builtin_memcpy(dest, reversed, sizeof(reversed));
}

void reverse_32bit_words(const uint8_t src[32], uint8_t dest[32])
{
    if ((uintptr_t)src % _Alignof(uint32_t) == 0 &&
        (uintptr_t)dest % _Alignof(uint32_t) == 0) {
        reverse_32bit_words_impl(__builtin_assume_aligned(src, _Alignof(uint32_t)),
                                __builtin_assume_aligned(dest, _Alignof(uint32_t)));
    } else {
        reverse_32bit_words_impl(src, dest);
    }
}

void reverse_endianness_per_word(uint8_t data[32])
{
    uint32_t words[8];

    // Work on aligned words while allowing an unaligned byte buffer.
    memcpy(words, data, sizeof(words));

    words[0] = __builtin_bswap32(words[0]);
    words[1] = __builtin_bswap32(words[1]);
    words[2] = __builtin_bswap32(words[2]);
    words[3] = __builtin_bswap32(words[3]);
    words[4] = __builtin_bswap32(words[4]);
    words[5] = __builtin_bswap32(words[5]);
    words[6] = __builtin_bswap32(words[6]);
    words[7] = __builtin_bswap32(words[7]);

    memcpy(data, words, sizeof(words));
}

static uint64_t load_le64(const uint8_t *data)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) {
        value |= (uint64_t)data[i] << (8 * i);
    }
    return value;
}

double le256todouble(const void *target)
{
    const uint8_t *data = target;
    /* Keep the original high-to-low accumulation and rounding. */
    double value = (double)load_le64(data + 24) * 0x1p192;
    value += (double)load_le64(data + 16) * 0x1p128;
    value += (double)load_le64(data + 8) * 0x1p64;
    value += (double)load_le64(data);
    return value;
}
