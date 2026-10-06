#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "serial.h"

/*
 * The host suite does not emulate the ASIC UART. These definitions only
 * satisfy production code paths that are discarded when a test does not
 * exercise serial I/O. A future serial-facing unit test should replace them
 * with an injected fake that models the behavior under test.
 */
int16_t SERIAL_rx(uint8_t *buffer, uint16_t size, uint16_t timeout_ms)
{
    (void) buffer;
    (void) size;
    (void) timeout_ms;
    return 0;
}

void SERIAL_clear_buffer(void)
{
}
