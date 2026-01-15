#include "unity.h"
#include "serial.h"
#include <string.h>

static size_t _row = 0, _col = 0;
static const uint8_t rx_test_pattern[2][12] = {
    { 0x55, 0xAA, 0x00, 0x011, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x9C, 0x00 },
    { 0x00, 0x55, 0xAA, 0x011, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x9C, 0x00 }
};

extern void SERIAL_set_read_fn(
    int (*fn)(uart_port_t, void*, uint16_t, TickType_t)
  );

int test_uart_read_bytes(uart_port_t port, void *buf, uint16_t size, TickType_t ticksToWait)
{
    uint8_t *dst = buf;
    for (uint16_t i = 0; i < size; ++i) {
        dst[i] = rx_test_pattern[_row][_col++];
        if (_col >= sizeof rx_test_pattern[_row]) {
            _col = 0;
            _row = (_row + 1) % 2;
        }
    }
    return size;
}

static void reset_test_pattern(void)
{
    _row = 0;
    _col = 0;
}

TEST_CASE("SERIAL_rx returns full test pattern", "[serial]")
{
    SERIAL_set_read_fn(test_uart_read_bytes);
    reset_test_pattern();

    uint8_t buf[24] = {0};
    int16_t read = SERIAL_rx(buf, 11, 100 /*ms*/);

    TEST_ASSERT_EQUAL_INT16(24, read);

    uint8_t expected[24];
    memcpy(expected +    0, rx_test_pattern[0], 11);
    memcpy(expected + 12+1, rx_test_pattern[1], 11);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buf, 24);
}
