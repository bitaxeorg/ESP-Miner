#include "utils.h"
#include <string.h>

void reverse_32bit_words(const uint8_t src[32], uint8_t dest[32])
{
    for (size_t i = 0; i < 8; ++i) {
        memcpy(dest + i * 4, src + (7 - i) * 4, 4);
    }
}

void reverse_endianness_per_word(uint8_t data[32])
{
    for (size_t i = 0; i < 32; i += 4) {
        uint8_t first = data[i];
        uint8_t second = data[i + 1];
        data[i] = data[i + 3];
        data[i + 1] = data[i + 2];
        data[i + 2] = second;
        data[i + 3] = first;
    }
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
