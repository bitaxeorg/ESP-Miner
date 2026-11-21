#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "unity.h"
#include "asic_common.h"
#include "crc.h"

#define TEST_FRAME_SIZE 9
#define TEST_STREAM_SIZE 64

esp_err_t serial_frame_test_receive_work(uint8_t *buffer, int buffer_size,
                                         uint64_t *out_timestamp_us);

static uint8_t rx_stream[TEST_STREAM_SIZE];
static size_t rx_stream_length;
static size_t rx_stream_offset;
static int64_t fake_time_us;
static bool invalid_uart_request;

int16_t serial_frame_test_uart_rx(uint8_t *buffer, uint16_t size,
                                  uint16_t timeout_ms)
{
    if (size != 1 || timeout_ms == 0) {
        invalid_uart_request = true;
        return -1;
    }
    if (rx_stream_offset == rx_stream_length) {
        return 0;
    }

    *buffer = rx_stream[rx_stream_offset++];
    return 1;
}

int64_t serial_frame_test_get_time(void)
{
    fake_time_us += 100;
    return fake_time_us;
}

static void set_rx_stream(const uint8_t *bytes, size_t length)
{
    TEST_ASSERT_TRUE(length <= sizeof(rx_stream));
    memcpy(rx_stream, bytes, length);
    rx_stream_length = length;
    rx_stream_offset = 0;
    fake_time_us = 0;
    invalid_uart_request = false;
}

static void make_valid_frame(uint8_t frame[TEST_FRAME_SIZE], uint8_t seed)
{
    frame[0] = 0xAA;
    frame[1] = 0x55;
    for (size_t index = 2; index < TEST_FRAME_SIZE; ++index) {
        frame[index] = (uint8_t)(seed + index);
    }

    /* The CRC is stored in the low five bits of the final response byte. */
    for (uint8_t crc = 0; crc < 32; ++crc) {
        frame[TEST_FRAME_SIZE - 1] = crc;
        if (crc5(frame + 2, TEST_FRAME_SIZE - 2) == 0) {
            return;
        }
    }
    TEST_FAIL_MESSAGE("Could not construct a CRC-valid test frame");
}

TEST_CASE("ASIC response parser skips leading UART noise and finds frame",
          "[asic][serial][regression]")
{
    const uint8_t noise[] = {0xFF, 0xFF, 0xFF, 0xAA};
    uint8_t frame[TEST_FRAME_SIZE];
    uint8_t stream[sizeof(noise) + TEST_FRAME_SIZE];
    uint8_t received[TEST_FRAME_SIZE] = {0};
    uint64_t timestamp_us = 0;

    make_valid_frame(frame, 0x20);
    memcpy(stream, noise, sizeof(noise));
    memcpy(stream + sizeof(noise), frame, sizeof(frame));
    set_rx_stream(stream, sizeof(stream));

    TEST_ASSERT_EQUAL(ESP_OK, serial_frame_test_receive_work(
                                  received, sizeof(received), &timestamp_us));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(frame, received, sizeof(frame));
    TEST_ASSERT_EQUAL_UINT32(sizeof(stream), rx_stream_offset);
    TEST_ASSERT_NOT_EQUAL(0, timestamp_us);
    TEST_ASSERT_FALSE(invalid_uart_request);
}

TEST_CASE("ASIC response parser recovers after bad CRC candidate",
          "[asic][serial][regression]")
{
    uint8_t corrupt_frame[TEST_FRAME_SIZE];
    uint8_t valid_frame[TEST_FRAME_SIZE];
    uint8_t stream[TEST_FRAME_SIZE * 2];
    uint8_t received[TEST_FRAME_SIZE] = {0};
    uint64_t timestamp_us = 0;

    make_valid_frame(corrupt_frame, 0x30);
    corrupt_frame[TEST_FRAME_SIZE - 1] ^= 0x01;
    TEST_ASSERT_NOT_EQUAL(0, crc5(corrupt_frame + 2, TEST_FRAME_SIZE - 2));
    make_valid_frame(valid_frame, 0x50);
    memcpy(stream, corrupt_frame, sizeof(corrupt_frame));
    memcpy(stream + sizeof(corrupt_frame), valid_frame, sizeof(valid_frame));
    set_rx_stream(stream, sizeof(stream));

    TEST_ASSERT_EQUAL(ESP_OK, serial_frame_test_receive_work(
                                  received, sizeof(received), &timestamp_us));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(valid_frame, received, sizeof(valid_frame));
    TEST_ASSERT_EQUAL_UINT32(sizeof(stream), rx_stream_offset);
    TEST_ASSERT_NOT_EQUAL(0, timestamp_us);
    TEST_ASSERT_FALSE(invalid_uart_request);
}
