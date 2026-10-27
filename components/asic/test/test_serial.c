#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "driver/uart.h"
#include "unity.h"

static int serial_test_uart_read(uart_port_t port, void *buf, uint32_t size, uint32_t ticks);
static int serial_test_uart_write(uart_port_t port, const void *data, size_t size);
static bool serial_test_uart_ready(uart_port_t port);
static void serial_test_hex(unsigned char *data, int size);

#define uart_read_bytes serial_test_uart_read
#define uart_write_bytes serial_test_uart_write
#define uart_is_driver_installed serial_test_uart_ready
#define prettyHex serial_test_hex
#define SERIAL_init serial_test_init
#define SERIAL_is_initialized serial_test_ready
#define SERIAL_set_baud serial_test_baud
#define SERIAL_send serial_test_send
#define SERIAL_rx serial_test_rx
#define SERIAL_rx_ticks serial_test_rx_ticks
#define SERIAL_debug_rx serial_test_debug_rx
#define SERIAL_clear_buffer serial_test_clear
#define TAG serial_test_shared_tag
#include "../serial.c"
#undef TAG
#define TAG serial_test_bzm_tag
#define BZM_SERIAL_init serial_test_bzm_init
#define BZM_SERIAL_set_baud serial_test_bzm_baud
#define BZM_SERIAL_wait_tx_done serial_test_bzm_wait
#define BZM_SERIAL_rx serial_test_bzm_rx
#include "../bzm/port/esp/serial.c"
#undef TAG

static bool installed;
static unsigned read_calls, write_calls, ready_calls, hex_calls;
static int read_result, write_result;
static uint32_t received_ticks;
static size_t received_size;
static const void *received_data;

static void fixture(void)
{
    installed = true;
    read_calls = write_calls = ready_calls = hex_calls = 0;
    read_result = write_result = 0;
    received_ticks = 0;
    received_size = 0;
    received_data = NULL;
}

static int serial_test_uart_read(uart_port_t port, void *buf, uint32_t size, uint32_t ticks)
{
    TEST_ASSERT_EQUAL(UART_NUM_1, port);
    ++read_calls;
    received_data = buf;
    received_size = size;
    received_ticks = ticks;
    if (read_result > 0) {
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(size, read_result);
        memset(buf, 0x42, read_result);
    }
    return read_result;
}

static int serial_test_uart_write(uart_port_t port, const void *data, size_t size)
{
    TEST_ASSERT_EQUAL(UART_NUM_1, port);
    ++write_calls;
    received_data = data;
    received_size = size;
    return write_result;
}

static bool serial_test_uart_ready(uart_port_t port)
{
    TEST_ASSERT_EQUAL(UART_NUM_1, port);
    ++ready_calls;
    return installed;
}

static void serial_test_hex(unsigned char *data, int size)
{
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_GREATER_THAN(0, size);
    ++hex_calls;
}

TEST_CASE("Shared serial preserves Bitmain timeout truncation", "[asic][serial]")
{
    uint8_t buf[16];
    const uint16_t timeouts[] = {0, 1, portTICK_PERIOD_MS - 1, portTICK_PERIOD_MS,
                                 portTICK_PERIOD_MS + 1, 20, 1000, UINT16_MAX};
    for (unsigned i = 0; i < sizeof(timeouts) / sizeof(timeouts[0]); ++i) {
        fixture();
        TEST_ASSERT_EQUAL_INT16(0, serial_test_rx(buf, sizeof(buf), timeouts[i]));
        TEST_ASSERT_EQUAL_UINT32(timeouts[i] / portTICK_PERIOD_MS, received_ticks);
        TEST_ASSERT_EQUAL_UINT32(1, read_calls);
        TEST_ASSERT_EQUAL_UINT32(0, ready_calls);
        TEST_ASSERT_EQUAL_PTR(buf, received_data);
        TEST_ASSERT_EQUAL_UINT32(sizeof(buf), received_size);
    }
}

TEST_CASE("BZM shared serial retains a tick for short nonzero waits", "[asic][serial][bzm]")
{
    uint8_t buf[16];
    fixture();
    TEST_ASSERT_EQUAL_INT16(0, serial_test_bzm_rx(buf, sizeof(buf), 0));
    TEST_ASSERT_EQUAL_UINT32(0, received_ticks);
    if (portTICK_PERIOD_MS > 1) {
        const uint16_t short_waits[] = {1, portTICK_PERIOD_MS - 1};
        for (unsigned i = 0; i < sizeof(short_waits) / sizeof(short_waits[0]); ++i) {
            fixture();
            TEST_ASSERT_EQUAL_INT16(0, serial_test_bzm_rx(buf, sizeof(buf), short_waits[i]));
            TEST_ASSERT_EQUAL_UINT32(1, received_ticks);
            TEST_ASSERT_EQUAL_UINT32(1, read_calls);
        }
    }
    const uint16_t waits[] = {portTICK_PERIOD_MS, portTICK_PERIOD_MS + 1, 20, 1000, UINT16_MAX};
    for (unsigned i = 0; i < sizeof(waits) / sizeof(waits[0]); ++i) {
        fixture();
        TEST_ASSERT_EQUAL_INT16(0, serial_test_bzm_rx(buf, sizeof(buf), waits[i]));
        TEST_ASSERT_EQUAL_UINT32(pdMS_TO_TICKS(waits[i]), received_ticks);
        TEST_ASSERT_EQUAL_UINT32(1, read_calls);
        TEST_ASSERT_EQUAL_PTR(buf, received_data);
        TEST_ASSERT_EQUAL_UINT32(sizeof(buf), received_size);
    }
}

TEST_CASE("Both serial receivers preserve data counts and UART failures", "[asic][serial]")
{
    int16_t (*const receivers[])(uint8_t *, uint16_t, uint16_t) = {serial_test_rx, serial_test_bzm_rx};
    const int results[] = {-1, 0, 3, 16};
    for (unsigned r = 0; r < sizeof(receivers) / sizeof(receivers[0]); ++r) {
        for (unsigned i = 0; i < sizeof(results) / sizeof(results[0]); ++i) {
            uint8_t buf[16];
            memset(buf, 0xa5, sizeof(buf));
            fixture();
            read_result = results[i];
            TEST_ASSERT_EQUAL_INT16(read_result, receivers[r](buf, sizeof(buf), 20));
            TEST_ASSERT_EQUAL_UINT32(1, read_calls);
            for (unsigned b = 0; b < sizeof(buf); ++b) {
                TEST_ASSERT_EQUAL_HEX8((int)b < read_result ? 0x42 : 0xa5, buf[b]);
            }
        }
    }
}

TEST_CASE("BZM rejects invalid reads without changing Bitmain readiness behavior", "[asic][serial][bzm]")
{
    uint8_t buf[16];
    fixture();
    TEST_ASSERT_EQUAL_INT16(-1, serial_test_bzm_rx(NULL, sizeof(buf), 20));
    TEST_ASSERT_EQUAL_INT16(-1, serial_test_bzm_rx(buf, 0, 20));
    installed = false;
    TEST_ASSERT_EQUAL_INT16(-1, serial_test_bzm_rx(buf, sizeof(buf), 20));
    TEST_ASSERT_EQUAL_UINT32(0, read_calls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, serial_test_bzm_wait(20));
    read_result = -1;
    TEST_ASSERT_EQUAL_INT16(-1, serial_test_rx(buf, sizeof(buf), 20));
    TEST_ASSERT_EQUAL_UINT32(1, read_calls);
}

TEST_CASE("Shared serial readiness follows the installed UART driver", "[asic][serial]")
{
    fixture();
    TEST_ASSERT_TRUE(serial_test_ready());
    installed = false;
    TEST_ASSERT_FALSE(serial_test_ready());
    TEST_ASSERT_EQUAL_UINT32(2, ready_calls);
}

TEST_CASE("Shared serial send preserves short writes errors and debug selection", "[asic][serial]")
{
    uint8_t data[] = {0x12, 0x34, 0x56};
    const int results[] = {-1, 0, 1, sizeof(data)};
    for (unsigned debug = 0; debug <= 1; ++debug) {
        for (unsigned i = 0; i < sizeof(results) / sizeof(results[0]); ++i) {
            fixture();
            write_result = results[i];
            TEST_ASSERT_EQUAL_INT(write_result, serial_test_send(data, sizeof(data), debug));
            TEST_ASSERT_EQUAL_UINT32(1, write_calls);
            TEST_ASSERT_EQUAL_PTR(data, received_data);
            TEST_ASSERT_EQUAL_UINT32(sizeof(data), received_size);
            TEST_ASSERT_EQUAL_UINT32(debug, hex_calls);
        }
    }
}
