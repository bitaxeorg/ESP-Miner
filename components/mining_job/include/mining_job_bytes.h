#ifndef MINING_JOB_BYTES_H_
#define MINING_JOB_BYTES_H_

#include <stdint.h>

// Byte writes also support unaligned Bitcoin headers and ASIC packets.
static inline void write_le32(uint8_t dest[4], uint32_t value)
{
    dest[0] = (uint8_t)value;
    dest[1] = (uint8_t)(value >> 8);
    dest[2] = (uint8_t)(value >> 16);
    dest[3] = (uint8_t)(value >> 24);
}

#endif
