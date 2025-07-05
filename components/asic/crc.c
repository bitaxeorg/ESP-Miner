
#include "crc.h"


// Poly x⁵ + x² + 1 MSB-first
uint8_t crc5(uint8_t *data, uint8_t len) {

    uint8_t crc = 0x1F;
    uint8_t bit_counter, byte_counter;

    for (byte_counter = 0; byte_counter < len; byte_counter++) {
        uint8_t byte = data[byte_counter];
        for (bit_counter = 0; bit_counter < 8; bit_counter++) {
            uint8_t bit = (byte >> 7) & 1;
            byte <<= 1;

            uint8_t new_bit = ((crc >> 4) ^ bit) & 1;
            crc = ((crc << 1) | new_bit) ^ (new_bit << 2);
            crc &= 0x1F;
        }
    }

    return crc;
}

// with loop unrolling
uint16_t crc16(uint8_t *data, uint16_t len)
{
    uint16_t crc = 0;

    while(len >= 4) {
        crc = crc16_table[(crc >> 8) ^ *data++] ^ (crc << 8);
        crc = crc16_table[(crc >> 8) ^ *data++] ^ (crc << 8);
        crc = crc16_table[(crc >> 8) ^ *data++] ^ (crc << 8);
        crc = crc16_table[(crc >> 8) ^ *data++] ^ (crc << 8);
        len -= 4;
    }

    while(len--) {
        crc = crc16_table[(crc >> 8) ^ *data++] ^ (crc << 8);
    }

    return crc;
}

uint16_t crc16_false(uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;

    while(len--) {
        crc = crc16_table[(crc >> 8) ^ *data++] ^ (crc << 8);
    }

    return crc;
}
