#ifndef BZM_BYTES_H
#define BZM_BYTES_H
#include <stdint.h>

static inline void bzm_store_le32(uint8_t bytes[4], uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) bytes[i] = (uint8_t)(value >> (i * 8));
}

static inline void bzm_store_be32(uint8_t bytes[4], uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) bytes[3 - i] = (uint8_t)(value >> (i * 8));
}
#endif
