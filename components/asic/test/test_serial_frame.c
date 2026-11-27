#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "unity.h"
#include "asic_common.h"
#include "crc.h"

#define TEST_FRAME_SIZE 9
#define TEST_MAX_FRAME_SIZE 11
#define TEST_STREAM_SIZE 128
#define TEST_MAX_READS 64

esp_err_t serial_frame_test_receive_work(uint8_t *buffer, int buffer_size,
                                         uint64_t *out_timestamp_us);

static uint8_t rx_stream[TEST_STREAM_SIZE];
static size_t rx_stream_length;
static size_t rx_stream_offset;
static int64_t fake_time_us;
static bool invalid_uart_request;
static uint16_t read_sizes[TEST_MAX_READS];
static uint16_t read_timeouts[TEST_MAX_READS];
static size_t read_count;
static int16_t read_script[TEST_MAX_READS];
static size_t read_script_length;
static size_t read_script_offset;
static unsigned flush_count;
static bool continuous_noise;
static int64_t read_duration_us;

int16_t serial_frame_test_uart_rx(uint8_t *buffer, uint16_t size,
                                  uint16_t timeout_ms)
{
    if (size == 0 || timeout_ms == 0 || timeout_ms > 10000) {
        invalid_uart_request = true;
        return -1;
    }
    TEST_ASSERT_LESS_THAN(TEST_MAX_READS, read_count);
    read_sizes[read_count] = size;
    read_timeouts[read_count++] = timeout_ms;
    fake_time_us += read_duration_us;

    int16_t scripted = size;
    if (read_script_offset < read_script_length) {
        scripted = read_script[read_script_offset++];
        if (scripted <= 0) {
            return scripted;
        }
    }
    if (continuous_noise) {
        memset(buffer, 0xFF, size);
        return size;
    }
    if (rx_stream_offset == rx_stream_length) {
        return 0;
    }

    size_t count = rx_stream_length - rx_stream_offset;
    if (count > size) {
        count = size;
    }
    if (count > (size_t) scripted) {
        count = scripted;
    }
    memcpy(buffer, rx_stream + rx_stream_offset, count);
    rx_stream_offset += count;
    return (int16_t) count;
}

void serial_frame_test_uart_flush(void)
{
    ++flush_count;
}

int64_t serial_frame_test_get_time(void)
{
    fake_time_us += 100;
    return fake_time_us;
}

static void set_rx_stream(const uint8_t *bytes, size_t length)
{
    TEST_ASSERT_TRUE(length <= sizeof(rx_stream));
    if (length) {
        memcpy(rx_stream, bytes, length);
    }
    rx_stream_length = length;
    rx_stream_offset = 0;
    fake_time_us = 0;
    invalid_uart_request = false;
    read_count = 0;
    read_script_length = 0;
    read_script_offset = 0;
    flush_count = 0;
    continuous_noise = false;
    read_duration_us = 0;
}

static void set_read_script(const int16_t *results, size_t count)
{
    TEST_ASSERT_TRUE(count <= TEST_MAX_READS);
    memcpy(read_script, results, count * sizeof(*results));
    read_script_length = count;
}

static void finish_frame_crc(uint8_t *frame, size_t size)
{
    /* The CRC is stored in the low five bits of the final response byte. */
    for (uint8_t crc = 0; crc < 32; ++crc) {
        frame[size - 1] = crc;
        if (crc5(frame + 2, size - 2) == 0) {
            return;
        }
    }
    TEST_FAIL_MESSAGE("Could not construct a CRC-valid test frame");
}

static void make_valid_frame(uint8_t *frame, size_t size, uint8_t seed)
{
    frame[0] = 0xAA;
    frame[1] = 0x55;
    for (size_t index = 2; index < size; ++index) {
        frame[index] = (uint8_t)(seed + index);
    }
    finish_frame_crc(frame, size);
}

static void assert_read_sizes(const uint16_t *expected, size_t count)
{
    TEST_ASSERT_EQUAL_UINT32(count, read_count);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(expected, read_sizes, count);
    TEST_ASSERT_FALSE(invalid_uart_request);
    TEST_ASSERT_EQUAL_UINT32(0, flush_count);
}

TEST_CASE("ASIC response parser skips leading UART noise and finds frame",
          "[asic][serial][regression]")
{
    const uint8_t noise[] = {0xFF, 0xFF, 0xFF, 0xAA};
    uint8_t frame[TEST_FRAME_SIZE];
    uint8_t stream[sizeof(noise) + TEST_FRAME_SIZE];
    uint8_t received[TEST_FRAME_SIZE] = {0};
    uint64_t timestamp_us = 0;

    make_valid_frame(frame, sizeof(frame), 0x20);
    memcpy(stream, noise, sizeof(noise));
    memcpy(stream + sizeof(noise), frame, sizeof(frame));
    set_rx_stream(stream, sizeof(stream));

    TEST_ASSERT_EQUAL(ESP_OK, serial_frame_test_receive_work(
                                  received, sizeof(received), &timestamp_us));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(frame, received, sizeof(frame));
    TEST_ASSERT_EQUAL_UINT32(sizeof(stream), rx_stream_offset);
    TEST_ASSERT_NOT_EQUAL(0, timestamp_us);
    const uint16_t expected_reads[] = {TEST_FRAME_SIZE, sizeof(noise)};
    assert_read_sizes(expected_reads, 2);
}

TEST_CASE("ASIC response parser recovers after bad CRC candidate",
          "[asic][serial][regression]")
{
    uint8_t corrupt_frame[TEST_FRAME_SIZE];
    uint8_t valid_frame[TEST_FRAME_SIZE];
    uint8_t next_frame[TEST_FRAME_SIZE];
    uint8_t stream[TEST_FRAME_SIZE * 3];
    uint8_t received[TEST_FRAME_SIZE] = {0};
    uint64_t timestamp_us = 0;

    make_valid_frame(corrupt_frame, sizeof(corrupt_frame), 0x30);
    corrupt_frame[TEST_FRAME_SIZE - 1] ^= 0x01;
    TEST_ASSERT_NOT_EQUAL(0, crc5(corrupt_frame + 2, TEST_FRAME_SIZE - 2));
    make_valid_frame(valid_frame, sizeof(valid_frame), 0x50);
    make_valid_frame(next_frame, sizeof(next_frame), 0x60);
    memcpy(stream, corrupt_frame, sizeof(corrupt_frame));
    memcpy(stream + sizeof(corrupt_frame), valid_frame, sizeof(valid_frame));
    memcpy(stream + sizeof(corrupt_frame) + sizeof(valid_frame), next_frame, sizeof(next_frame));
    set_rx_stream(stream, sizeof(stream));

    TEST_ASSERT_EQUAL(ESP_OK, serial_frame_test_receive_work(
                                  received, sizeof(received), &timestamp_us));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(valid_frame, received, sizeof(valid_frame));
    TEST_ASSERT_EQUAL_UINT32(TEST_FRAME_SIZE * 2, rx_stream_offset);
    TEST_ASSERT_NOT_EQUAL(0, timestamp_us);
    TEST_ASSERT_EQUAL(ESP_OK, serial_frame_test_receive_work(received, sizeof(received), NULL));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(next_frame, received, sizeof(next_frame));
    TEST_ASSERT_EQUAL_UINT32(sizeof(stream), rx_stream_offset);
    const uint16_t expected_reads[] = {TEST_FRAME_SIZE, 1, 1, TEST_FRAME_SIZE - 2, TEST_FRAME_SIZE};
    assert_read_sizes(expected_reads, 5);
}

TEST_CASE("ASIC response parser reads aligned frames in bulk", "[asic][serial][regression]")
{
    for (size_t size = TEST_FRAME_SIZE; size <= TEST_MAX_FRAME_SIZE; size += 2) {
        uint8_t stream[TEST_MAX_FRAME_SIZE * 2];
        uint8_t received[TEST_MAX_FRAME_SIZE];
        uint64_t timestamp_us = 0;
        make_valid_frame(stream, size, 0x20);
        make_valid_frame(stream + size, size, 0x40);
        set_rx_stream(stream, size * 2);

        TEST_ASSERT_EQUAL(ESP_OK, serial_frame_test_receive_work(received, size, &timestamp_us));
        TEST_ASSERT_EQUAL_HEX8_ARRAY(stream, received, size);
        TEST_ASSERT_EQUAL_UINT32(size, rx_stream_offset);
        TEST_ASSERT_NOT_EQUAL(0, timestamp_us);
        TEST_ASSERT_EQUAL(ESP_OK, serial_frame_test_receive_work(received, size, NULL));
        TEST_ASSERT_EQUAL_HEX8_ARRAY(stream + size, received, size);
        TEST_ASSERT_EQUAL_UINT32(size * 2, rx_stream_offset);
        const uint16_t expected_reads[] = {size, size};
        assert_read_sizes(expected_reads, 2);
    }
}

TEST_CASE("ASIC response parser rejects a bad preamble even with valid CRC", "[asic][serial][regression]")
{
    uint8_t stream[TEST_FRAME_SIZE * 2];
    uint8_t received[TEST_FRAME_SIZE];
    make_valid_frame(stream, TEST_FRAME_SIZE, 0x20);
    stream[0] = 0xFF;
    TEST_ASSERT_EQUAL(0, crc5(stream + 2, TEST_FRAME_SIZE - 2));
    make_valid_frame(stream + TEST_FRAME_SIZE, TEST_FRAME_SIZE, 0x40);
    set_rx_stream(stream, sizeof(stream));

    TEST_ASSERT_EQUAL(ESP_OK, serial_frame_test_receive_work(received, sizeof(received), NULL));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(stream + TEST_FRAME_SIZE, received, sizeof(received));
    const uint16_t expected_reads[] = {TEST_FRAME_SIZE, 1, 1, TEST_FRAME_SIZE - 2};
    assert_read_sizes(expected_reads, 4);
}

TEST_CASE("ASIC response parser accumulates short reads without seeking", "[asic][serial][regression]")
{
    uint8_t frame[TEST_FRAME_SIZE];
    uint8_t received[TEST_FRAME_SIZE];
    make_valid_frame(frame, sizeof(frame), 0x20);
    set_rx_stream(frame, sizeof(frame));
    const int16_t script[] = {1, 2, 6};
    set_read_script(script, 3);

    TEST_ASSERT_EQUAL(ESP_OK, serial_frame_test_receive_work(received, sizeof(received), NULL));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(frame, received, sizeof(frame));
    const uint16_t expected_reads[] = {9, 8, 6};
    assert_read_sizes(expected_reads, 3);
}

TEST_CASE("ASIC response parser preserves a preamble inside valid payload", "[asic][serial][regression]")
{
    uint8_t frame[TEST_FRAME_SIZE];
    uint8_t received[TEST_FRAME_SIZE];
    make_valid_frame(frame, sizeof(frame), 0x20);
    frame[4] = 0xAA;
    frame[5] = 0x55;
    finish_frame_crc(frame, sizeof(frame));
    set_rx_stream(frame, sizeof(frame));

    TEST_ASSERT_EQUAL(ESP_OK, serial_frame_test_receive_work(received, sizeof(received), NULL));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(frame, received, sizeof(frame));
    const uint16_t expected_reads[] = {TEST_FRAME_SIZE};
    assert_read_sizes(expected_reads, 1);
}

TEST_CASE("ASIC response parser retains overlapping frame after CRC failure", "[asic][serial][regression]")
{
    uint8_t stream[TEST_FRAME_SIZE + 3] = {0xAA, 0x55, 0xFF};
    uint8_t received[TEST_FRAME_SIZE];
    make_valid_frame(stream + 3, TEST_FRAME_SIZE, 0x60);
    if (crc5(stream + 2, TEST_FRAME_SIZE - 2) == 0) {
        stream[2] ^= 1;
    }
    TEST_ASSERT_NOT_EQUAL(0, crc5(stream + 2, TEST_FRAME_SIZE - 2));
    set_rx_stream(stream, sizeof(stream));

    TEST_ASSERT_EQUAL(ESP_OK, serial_frame_test_receive_work(received, sizeof(received), NULL));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(stream + 3, received, sizeof(received));
    TEST_ASSERT_EQUAL_UINT32(sizeof(stream), rx_stream_offset);
    const uint16_t expected_reads[] = {TEST_FRAME_SIZE, 3};
    assert_read_sizes(expected_reads, 2);
}

TEST_CASE("ASIC response parser retains a trailing partial preamble", "[asic][serial][regression]")
{
    uint8_t stream[TEST_FRAME_SIZE * 2 - 1];
    uint8_t received[TEST_FRAME_SIZE];
    memset(stream, 0xFF, TEST_FRAME_SIZE - 1);
    make_valid_frame(stream + TEST_FRAME_SIZE - 1, TEST_FRAME_SIZE, 0x20);
    set_rx_stream(stream, sizeof(stream));

    TEST_ASSERT_EQUAL(ESP_OK, serial_frame_test_receive_work(received, sizeof(received), NULL));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(stream + TEST_FRAME_SIZE - 1, received, sizeof(received));
    const uint16_t expected_reads[] = {TEST_FRAME_SIZE, 1, TEST_FRAME_SIZE - 2};
    assert_read_sizes(expected_reads, 3);
}

TEST_CASE("ASIC response parser seeks through noise and repeated preamble bytes", "[asic][serial][regression]")
{
    uint8_t stream[TEST_FRAME_SIZE * 2 + 2];
    uint8_t received[TEST_FRAME_SIZE];
    memset(stream, 0xFF, TEST_FRAME_SIZE + 1);
    stream[TEST_FRAME_SIZE + 1] = 0xAA;
    make_valid_frame(stream + TEST_FRAME_SIZE + 2, TEST_FRAME_SIZE, 0x20);
    set_rx_stream(stream, sizeof(stream));

    TEST_ASSERT_EQUAL(ESP_OK, serial_frame_test_receive_work(received, sizeof(received), NULL));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(stream + TEST_FRAME_SIZE + 2, received, sizeof(received));
    const uint16_t expected_reads[] = {TEST_FRAME_SIZE, 1, 1, 1, 1, TEST_FRAME_SIZE - 2};
    assert_read_sizes(expected_reads, 6);
}

TEST_CASE("ASIC response parser rejects invalid sizes without UART access", "[asic][serial][regression]")
{
    uint8_t received[TEST_FRAME_SIZE];
    set_rx_stream(NULL, 0);
    TEST_ASSERT_EQUAL(ESP_FAIL, serial_frame_test_receive_work(received, 0, NULL));
    TEST_ASSERT_EQUAL(ESP_FAIL, serial_frame_test_receive_work(received, 2, NULL));
    TEST_ASSERT_EQUAL_UINT32(0, read_count);
    TEST_ASSERT_EQUAL_UINT32(0, flush_count);
}

TEST_CASE("ASIC response parser fails on UART error or empty read", "[asic][serial][regression]")
{
    uint8_t frame[TEST_FRAME_SIZE];
    uint8_t received[TEST_FRAME_SIZE];
    make_valid_frame(frame, sizeof(frame), 0x20);
    for (int result = -1; result <= 0; ++result) {
        for (int partial = 0; partial <= 1; ++partial) {
            uint64_t timestamp_us = 0;
            set_rx_stream(frame, sizeof(frame));
            const int16_t script[] = {partial ? 4 : result, result};
            set_read_script(script, partial ? 2 : 1);
            TEST_ASSERT_EQUAL(ESP_FAIL,
                              serial_frame_test_receive_work(received, sizeof(received), &timestamp_us));
            TEST_ASSERT_NOT_EQUAL(0, timestamp_us);
            const uint16_t expected_reads[] = {TEST_FRAME_SIZE, TEST_FRAME_SIZE - 4};
            assert_read_sizes(expected_reads, partial ? 2 : 1);
        }
    }
}

TEST_CASE("ASIC response parser fails on UART error during recovery", "[asic][serial][regression]")
{
    uint8_t noise[TEST_FRAME_SIZE];
    uint8_t received[TEST_FRAME_SIZE];
    memset(noise, 0xFF, sizeof(noise));
    for (int result = -1; result <= 0; ++result) {
        uint64_t timestamp_us = 0;
        set_rx_stream(noise, sizeof(noise));
        const int16_t script[] = {TEST_FRAME_SIZE, result};
        set_read_script(script, 2);
        TEST_ASSERT_EQUAL(ESP_FAIL, serial_frame_test_receive_work(received, sizeof(received), &timestamp_us));
        TEST_ASSERT_NOT_EQUAL(0, timestamp_us);
        const uint16_t expected_reads[] = {TEST_FRAME_SIZE, 1};
        assert_read_sizes(expected_reads, 2);
    }
}

TEST_CASE("ASIC response parser keeps one deadline while seeking", "[asic][serial][regression]")
{
    uint8_t received[TEST_FRAME_SIZE];
    uint64_t timestamp_us = 0;
    set_rx_stream(NULL, 0);
    continuous_noise = true;
    read_duration_us = 1000000;

    TEST_ASSERT_EQUAL(ESP_FAIL, serial_frame_test_receive_work(received, sizeof(received), &timestamp_us));
    TEST_ASSERT_TRUE(fake_time_us >= 10000000 && fake_time_us < 10010000);
    TEST_ASSERT_EQUAL(fake_time_us, timestamp_us);
    TEST_ASSERT_TRUE(read_count > 1 && read_count <= 10);
    TEST_ASSERT_EQUAL_UINT16(TEST_FRAME_SIZE, read_sizes[0]);
    for (size_t i = 1; i < read_count; ++i) {
        TEST_ASSERT_EQUAL_UINT16(1, read_sizes[i]);
        TEST_ASSERT_TRUE(read_timeouts[i] < read_timeouts[i - 1]);
    }
    TEST_ASSERT_FALSE(invalid_uart_request);
    TEST_ASSERT_EQUAL_UINT32(0, flush_count);
}

TEST_CASE("ASIC response parser keeps one deadline across short reads", "[asic][serial][regression]")
{
    uint8_t frame[TEST_FRAME_SIZE];
    uint8_t received[TEST_FRAME_SIZE];
    uint64_t timestamp_us = 0;
    make_valid_frame(frame, sizeof(frame), 0x20);
    set_rx_stream(frame, sizeof(frame));
    const int16_t script[] = {1, 1, 1, 1, 1, 1, 1, 1, 1};
    set_read_script(script, 9);
    read_duration_us = 2000000;

    TEST_ASSERT_EQUAL(ESP_FAIL, serial_frame_test_receive_work(received, sizeof(received), &timestamp_us));
    TEST_ASSERT_TRUE(fake_time_us >= 10000000 && fake_time_us < 10010000);
    TEST_ASSERT_EQUAL(fake_time_us, timestamp_us);
    TEST_ASSERT_EQUAL_UINT32(5, rx_stream_offset);
    const uint16_t expected_reads[] = {9, 8, 7, 6, 5};
    assert_read_sizes(expected_reads, 5);
    for (size_t i = 1; i < read_count; ++i) {
        TEST_ASSERT_TRUE(read_timeouts[i] < read_timeouts[i - 1]);
    }
}
