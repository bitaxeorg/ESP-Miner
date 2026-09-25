#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "bzm/frame_parser.h"
#include "bzm/registers.h"
#include "bzm/telemetry_internal.h"
#include "bzm/transport.h"
#include "bzm/serial.h"
#include "unity.h"

#define CAPTURED_FRAME_COUNT 8

typedef struct
{
    size_t count;
    bzm_frame_t frames[CAPTURED_FRAME_COUNT];
} frame_capture_t;

typedef struct
{
    uint8_t bytes[1200];
    size_t length;
    size_t cursor;
    size_t calls;
} serial_reader_mock_t;

static int16_t serial_reader_mock(void * context, uint8_t * data, uint16_t size, uint16_t timeout_ms)
{
    serial_reader_mock_t * mock = context;
    TEST_ASSERT_NOT_NULL(mock);
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_GREATER_THAN_UINT16(0, size);
    if (mock->calls++ == 0) {
        TEST_ASSERT_EQUAL_UINT16(25, timeout_ms);
        TEST_ASSERT_EQUAL_UINT16(1, size);
    } else {
        TEST_ASSERT_EQUAL_UINT16(0, timeout_ms);
    }
    if (mock->cursor == mock->length)
        return 0;
    size_t available = mock->length - mock->cursor;
    size_t count = available < size ? available : size;
    memcpy(data, mock->bytes + mock->cursor, count);
    mock->cursor += count;
    return (int16_t) count;
}

static void capture_frame(void * context, const bzm_frame_t * frame)
{
    frame_capture_t * capture = context;
    TEST_ASSERT_LESS_THAN(CAPTURED_FRAME_COUNT, capture->count);
    capture->frames[capture->count++] = *frame;
}

TEST_CASE("BZM UART ring covers the bounded full-dispatch polling blackout", "[asic][bzm][serial][qemu-integration]")
{
    TEST_ASSERT_TRUE(BZM_SERIAL_buffer_capacity_covers(BZM_SERIAL_RX_BUFFER_BYTES, BZM_SERIAL_RX_DESIGN_RATE_BYTES_PER_SECOND,
                                                   BZM_SERIAL_RX_MAX_DISPATCH_BLACKOUT_MS));
    TEST_ASSERT_FALSE(
        BZM_SERIAL_buffer_capacity_covers(2U * 1024U, BZM_SERIAL_RX_DESIGN_RATE_BYTES_PER_SECOND, BZM_SERIAL_RX_MAX_DISPATCH_BLACKOUT_MS));
    TEST_ASSERT_TRUE(BZM_SERIAL_buffer_capacity_covers(2000U, 2000U, 1000U));
    TEST_ASSERT_FALSE(BZM_SERIAL_buffer_capacity_covers(1999U, 2000U, 1000U));

    TEST_ASSERT_TRUE(BZM_SERIAL_fifo_reserve_covers(BZM_SERIAL_HARDWARE_FIFO_BYTES, BZM_SERIAL_RX_FIFO_FULL_THRESHOLD,
                                                BZM_SERIAL_WIRE_BYTES_PER_SECOND, BZM_SERIAL_MAX_ISR_LATENCY_US));
    TEST_ASSERT_FALSE(
        BZM_SERIAL_fifo_reserve_covers(BZM_SERIAL_HARDWARE_FIFO_BYTES, 120U, BZM_SERIAL_WIRE_BYTES_PER_SECOND, BZM_SERIAL_MAX_ISR_LATENCY_US));
    TEST_ASSERT_FALSE(BZM_SERIAL_fifo_reserve_covers(128U, 128U, 1U, 1U));
}

TEST_CASE("BZM parser preserves fragmented and interleaved TDM frames", "[asic][bzm][frame-parser][qemu-integration]")
{
    static const uint8_t stream[] = {
        0x99, 0x0a, 0x01, 0x83, 0x45, 0x78, 0x56, 0x34, 0x12, 0x17, 0x0d, 0x14, 0x03, 0xde, 0xad, 0xbe,
        0xef, 0x1e, 0x0d, 0xc9, 0x00, 0x98, 0x00, 0x60, 0xd0, 0xaa, 0xe2, 0x28, 0x0f, '2',  'Z',  'B',
    };
    static const size_t fragment_sizes[] = {1, 2, 1, 5, 3, 7, 1, 4, 2, 9};

    frame_capture_t capture = {0};
    bzm_frame_parser_t parser;
    bzm_frame_parser_init(&parser, capture_frame, &capture);
    TEST_ASSERT_TRUE(bzm_frame_parser_expect_register(&parser, 0x14, 4));

    size_t cursor = 0;
    size_t fragment = 0;
    while (cursor < sizeof(stream)) {
        size_t length = fragment_sizes[fragment++ % (sizeof(fragment_sizes) / sizeof(fragment_sizes[0]))];
        if (length > sizeof(stream) - cursor)
            length = sizeof(stream) - cursor;
        bzm_frame_parser_feed(&parser, stream + cursor, length, 1000 + cursor);
        cursor += length;
    }

    TEST_ASSERT_EQUAL_UINT32(4, capture.count);
    TEST_ASSERT_EQUAL(BZM_FRAME_RESULT, capture.frames[0].type);
    TEST_ASSERT_EQUAL_HEX8(0x0a, capture.frames[0].asic_id);
    TEST_ASSERT_EQUAL_UINT32(BZM_RESULT_FRAME_SIZE, capture.frames[0].payload_length);
    TEST_ASSERT_EQUAL_HEX8(0x83, capture.frames[0].payload[0]);
    TEST_ASSERT_EQUAL_HEX8(0x0d, capture.frames[0].payload[7]);

    TEST_ASSERT_EQUAL(BZM_FRAME_REGISTER, capture.frames[1].type);
    TEST_ASSERT_EQUAL_HEX8(0x14, capture.frames[1].asic_id);
    TEST_ASSERT_EQUAL_UINT32(4, capture.frames[1].payload_length);
    TEST_ASSERT_EQUAL_HEX8(0xde, capture.frames[1].payload[0]);
    TEST_ASSERT_EQUAL_HEX8(0xef, capture.frames[1].payload[3]);

    TEST_ASSERT_EQUAL(BZM_FRAME_TELEMETRY, capture.frames[2].type);
    TEST_ASSERT_EQUAL_HEX8(0x1e, capture.frames[2].asic_id);
    TEST_ASSERT_EQUAL_UINT32(BZM_TDM_TELEMETRY_PAYLOAD_SIZE, capture.frames[2].payload_length);
    TEST_ASSERT_EQUAL_HEX8(0xe2, capture.frames[2].payload[7]);

    TEST_ASSERT_EQUAL(BZM_FRAME_NOOP, capture.frames[3].type);
    TEST_ASSERT_EQUAL_HEX8(0x28, capture.frames[3].asic_id);
    TEST_ASSERT_EQUAL_UINT32(BZM_TDM_NOOP_PAYLOAD_SIZE, capture.frames[3].payload_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("2ZB", capture.frames[3].payload, 3);

    TEST_ASSERT_EQUAL_UINT32(4, parser.emitted_frames);
    TEST_ASSERT_EQUAL_UINT32(1, parser.discarded_bytes);
    TEST_ASSERT_EQUAL_UINT32(0, bzm_frame_parser_pending_bytes(&parser));
}

TEST_CASE("BZM parser waits for complete frames without consuming prefixes", "[asic][bzm][frame-parser][qemu-integration]")
{
    static const uint8_t result[] = {
        0x0a, 0x01, 0x83, 0x45, 0x78, 0x56, 0x34, 0x12, 0x17, 0x0d,
    };
    frame_capture_t capture = {0};
    bzm_frame_parser_t parser;
    bzm_frame_parser_init(&parser, capture_frame, &capture);

    TEST_ASSERT_EQUAL_UINT32(0, bzm_frame_parser_feed(&parser, result, 6, 100));
    TEST_ASSERT_EQUAL_UINT32(6, bzm_frame_parser_pending_bytes(&parser));
    TEST_ASSERT_EQUAL_UINT32(1, bzm_frame_parser_feed(&parser, result + 6, 4, 200));
    TEST_ASSERT_EQUAL_UINT32(1, capture.count);
    TEST_ASSERT_EQUAL_UINT32(200, (uint32_t) capture.frames[0].timestamp_us);
    TEST_ASSERT_EQUAL_UINT32(0, bzm_frame_parser_pending_bytes(&parser));
}

TEST_CASE("BZM parser accepts a fixed-length broadcast discovery noop", "[asic][bzm][frame-parser][qemu-integration]")
{
    static const uint8_t noop[] = {
        BZM_TDM_BROADCAST_ASIC_ID, BZM_FRAME_NOOP, '2', 'Z', 'B',
    };
    frame_capture_t capture = {0};
    bzm_frame_parser_t parser;
    bzm_frame_parser_init(&parser, capture_frame, &capture);

    TEST_ASSERT_EQUAL_UINT32(1, bzm_frame_parser_feed(&parser, noop, sizeof(noop), 250));
    TEST_ASSERT_EQUAL_UINT32(1, capture.count);
    TEST_ASSERT_EQUAL(BZM_FRAME_NOOP, capture.frames[0].type);
    TEST_ASSERT_EQUAL_HEX8(BZM_TDM_BROADCAST_ASIC_ID, capture.frames[0].asic_id);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("2ZB", capture.frames[0].payload, BZM_TDM_NOOP_PAYLOAD_SIZE);
}

TEST_CASE("BZM parser accepts only the four spaced ASIC wire IDs", "[asic][bzm][frame-parser][tdm][qemu-integration]")
{
    frame_capture_t capture = {0};
    bzm_frame_parser_t parser;
    bzm_frame_parser_init(&parser, capture_frame, &capture);

    for (size_t index = 0; index < BZM_MAX_ASIC_COUNT; ++index) {
        uint8_t noop[] = {bzm_asic_wire_ids[index], BZM_FRAME_NOOP, '2', 'Z', 'B'};
        TEST_ASSERT_EQUAL_UINT32(1, bzm_frame_parser_feed(&parser, noop, sizeof(noop), 100 + index));
        TEST_ASSERT_EQUAL_HEX8(bzm_asic_wire_ids[index], capture.frames[index].asic_id);
    }

    static const uint8_t adjacent_id_noise[] = {0x0b, BZM_FRAME_NOOP, '2', 'Z', 'B'};
    TEST_ASSERT_EQUAL_UINT32(0, bzm_frame_parser_feed(&parser, adjacent_id_noise, sizeof(adjacent_id_noise), 200));
    TEST_ASSERT_EQUAL_UINT32(BZM_MAX_ASIC_COUNT, capture.count);
    TEST_ASSERT_GREATER_THAN_UINT32(0, parser.discarded_bytes);
}

TEST_CASE("BZM register reply length must be reserved before parsing", "[asic][bzm][frame-parser][qemu-integration]")
{
    static const uint8_t stream[] = {
        0x0a, 0x03, 0xaa, 0xbb, 0x0a, 0x0f, '2', 'Z', 'B',
    };
    frame_capture_t capture = {0};
    bzm_frame_parser_t parser;
    bzm_frame_parser_init(&parser, capture_frame, &capture);

    TEST_ASSERT_FALSE(bzm_frame_parser_expect_register(&parser, 0x0b, 4));
    TEST_ASSERT_FALSE(bzm_frame_parser_expect_register(&parser, 0x0a, 0));
    TEST_ASSERT_FALSE(bzm_frame_parser_expect_register(&parser, 0x0a, 33));
    TEST_ASSERT_EQUAL_UINT32(1, bzm_frame_parser_feed(&parser, stream, sizeof(stream), 300));
    TEST_ASSERT_EQUAL_UINT32(1, capture.count);
    TEST_ASSERT_EQUAL(BZM_FRAME_NOOP, capture.frames[0].type);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(4, parser.discarded_bytes);

    bzm_frame_parser_init(&parser, capture_frame, &capture);
    TEST_ASSERT_TRUE(bzm_frame_parser_expect_register(&parser, 0x0a, 2));
    TEST_ASSERT_FALSE(bzm_frame_parser_expect_register(&parser, 0x0a, 2));
    TEST_ASSERT_TRUE(bzm_frame_parser_cancel_register(&parser, 0x0a));
    TEST_ASSERT_FALSE(bzm_frame_parser_cancel_register(&parser, 0x0a));
}

TEST_CASE("BZM generation-two telemetry decodes every safety field", "[asic][bzm][telemetry][qemu-integration]")
{
    /* temp=0x900, ch0=0x1800, ch1=0x1810, ch2=0x0aab; sensors enabled,
     * combined PLL0/PLL1 lock set. TDM byte-7 bits 5 and 6 are reserved. */
    static const uint8_t payload[BZM_TDM_TELEMETRY_PAYLOAD_SIZE] = {
        0xc9, 0x00, 0x98, 0x00, 0x60, 0xd0, 0xaa, 0xe2,
    };
    bzm_telemetry_sample_t sample;
    TEST_ASSERT_TRUE(bzm_telemetry_decode(0x0a, payload, sizeof(payload), 123456, &sample));

    TEST_ASSERT_EQUAL_HEX8(0x0a, sample.asic_id);
    TEST_ASSERT_EQUAL_UINT32(123456, (uint32_t) sample.timestamp_us);
    TEST_ASSERT_TRUE(sample.received);
    TEST_ASSERT_EQUAL_HEX16(0x0900, sample.temperature_code);
    TEST_ASSERT_EQUAL_HEX16(0x1800, sample.ch0_code);
    TEST_ASSERT_EQUAL_HEX16(0x1810, sample.ch1_code);
    TEST_ASSERT_EQUAL_HEX16(0x0aab, sample.ch2_code);
    TEST_ASSERT_TRUE(sample.thermal_enabled);
    TEST_ASSERT_TRUE(sample.thermal_validity);
    TEST_ASSERT_FALSE(sample.thermal_fault);
    TEST_ASSERT_FALSE(sample.thermal_trip);
    TEST_ASSERT_TRUE(sample.thermal_valid);
    TEST_ASSERT_TRUE(sample.voltage_enabled);
    TEST_ASSERT_FALSE(sample.voltage_fault);
    TEST_ASSERT_FALSE(sample.voltage_trip);
    TEST_ASSERT_TRUE(sample.voltage_valid);
    TEST_ASSERT_TRUE(sample.pll_locked);
    TEST_ASSERT_TRUE(sample.valid);
    TEST_ASSERT_FALSE(sample.trip);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, bzm_telemetry_temperature_from_code(0x0900), sample.temperature_c);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, bzm_telemetry_voltage_from_code_mv(0x1800), sample.ch0_mv);

    TEST_ASSERT_FALSE(bzm_telemetry_decode(0x0b, payload, sizeof(payload), 0, &sample));
    TEST_ASSERT_FALSE(bzm_telemetry_decode(0x0a, payload, sizeof(payload) - 1, 0, &sample));
}

TEST_CASE("BZM telemetry faults and trips fail the aggregate validity", "[asic][bzm][telemetry][qemu-integration]")
{
    static const uint8_t payload[BZM_TDM_TELEMETRY_PAYLOAD_SIZE] = {
        0xf9, 0x00, 0xd8, 0x00, 0x60, 0xd0, 0xaa, 0x12,
    };
    bzm_telemetry_sample_t sample;
    TEST_ASSERT_TRUE(bzm_telemetry_decode(0x28, payload, sizeof(payload), 100, &sample));
    TEST_ASSERT_TRUE(sample.thermal_fault);
    TEST_ASSERT_TRUE(sample.thermal_trip);
    TEST_ASSERT_TRUE(sample.voltage_fault);
    TEST_ASSERT_TRUE(sample.voltage_trip);
    TEST_ASSERT_FALSE(sample.thermal_valid);
    TEST_ASSERT_FALSE(sample.voltage_valid);
    TEST_ASSERT_FALSE(sample.valid);
    TEST_ASSERT_TRUE(sample.trip);
    TEST_ASSERT_FALSE(sample.pll_locked);
}

TEST_CASE("BZM telemetry distinguishes immediate trips from confirmable frame anomalies", "[asic][bzm][telemetry][qemu-integration]")
{
    bzm_telemetry_sample_t sample = {
        .received = true,
        .thermal_enabled = true,
        .voltage_enabled = true,
        .pll_locked = false,
        .ch2_mv = -153.43f,
    };

    TEST_ASSERT_FALSE(bzm_telemetry_sample_has_immediate_trip(NULL));
    TEST_ASSERT_FALSE(bzm_telemetry_sample_has_immediate_trip(&sample));

    sample.voltage_trip = true;
    sample.trip = true;
    TEST_ASSERT_TRUE(bzm_telemetry_sample_has_immediate_trip(&sample));
    sample.voltage_trip = false;
    sample.trip = false;

    sample.thermal_fault = true;
    TEST_ASSERT_FALSE(bzm_telemetry_sample_has_immediate_trip(&sample));
    sample.thermal_fault = false;
    sample.voltage_fault = true;
    TEST_ASSERT_FALSE(bzm_telemetry_sample_has_immediate_trip(&sample));
    sample.voltage_fault = false;
    sample.thermal_enabled = false;
    TEST_ASSERT_FALSE(bzm_telemetry_sample_has_immediate_trip(&sample));
    sample.thermal_enabled = true;
    sample.voltage_enabled = false;
    TEST_ASSERT_FALSE(bzm_telemetry_sample_has_immediate_trip(&sample));
}

TEST_CASE("BZM telemetry freshness bounds and NaN checks fail closed", "[asic][bzm][telemetry][qemu-integration]")
{
    static const uint8_t payload[BZM_TDM_TELEMETRY_PAYLOAD_SIZE] = {
        0xc9, 0x00, 0x98, 0x00, 0x60, 0xd0, 0xaa, 0xe2,
    };
    bzm_telemetry_sample_t sample;
    TEST_ASSERT_TRUE(bzm_telemetry_decode(0x0a, payload, sizeof(payload), 1000, &sample));
    bzm_telemetry_bounds_t bounds = {
        .temperature_min_c = sample.temperature_c - 1.0f,
        .temperature_max_c = sample.temperature_c + 1.0f,
        .ch0_min_mv = sample.ch0_mv - 1.0f,
        .ch0_max_mv = sample.ch0_mv + 1.0f,
        .ch1_min_mv = sample.ch1_mv - 1.0f,
        .ch1_max_mv = sample.ch1_mv + 1.0f,
        .ch2_abs_max_mv = fabsf(sample.ch2_mv) + 1.0f,
        .max_stack_spread_mv = 1000.0f,
    };

    TEST_ASSERT_TRUE(bzm_telemetry_sample_is_fresh(&sample, 1100, 100));
    TEST_ASSERT_FALSE(bzm_telemetry_sample_is_fresh(&sample, 1101, 100));
    TEST_ASSERT_FALSE(bzm_telemetry_sample_is_fresh(&sample, 999, 100));
    TEST_ASSERT_TRUE(bzm_telemetry_sample_is_within_bounds(&sample, &bounds));
    TEST_ASSERT_TRUE(bzm_telemetry_sample_is_safe(&sample, 1100, 100, &bounds, true));

    sample.ch2_mv = bounds.ch2_abs_max_mv + 0.1f;
    TEST_ASSERT_FALSE(bzm_telemetry_sample_is_within_bounds(&sample, &bounds));
    TEST_ASSERT_TRUE(bzm_telemetry_sample_is_safe_except_ch2(&sample, 1100, 100, &bounds, true));

    sample.voltage_fault = true;
    sample.voltage_valid = false;
    sample.valid = false;
    TEST_ASSERT_TRUE(bzm_telemetry_sample_is_safe_except_ch2(&sample, 1100, 100, &bounds, true));
    sample.ch2_mv = 0.0f;
    TEST_ASSERT_FALSE(bzm_telemetry_sample_is_safe_except_ch2(&sample, 1100, 100, &bounds, true));
    sample.voltage_fault = false;
    sample.voltage_valid = true;
    sample.valid = true;

    sample.ch1_mv = sample.ch0_mv + 10.0f;
    bounds.ch1_min_mv = sample.ch1_mv - 1.0f;
    bounds.ch1_max_mv = sample.ch1_mv + 1.0f;
    bounds.max_stack_spread_mv = 9.0f;
    TEST_ASSERT_FALSE(bzm_telemetry_sample_is_within_bounds(&sample, &bounds));
    bounds.max_stack_spread_mv = 10.0f;
    TEST_ASSERT_TRUE(bzm_telemetry_sample_is_within_bounds(&sample, &bounds));
    bounds.max_stack_spread_mv = 1000.0f;

    sample.temperature_c = NAN;
    TEST_ASSERT_FALSE(bzm_telemetry_value_in_bounds(NAN, 0.0f, 1.0f));
    TEST_ASSERT_FALSE(bzm_telemetry_value_in_bounds(0.5f, NAN, 1.0f));
    TEST_ASSERT_FALSE(bzm_telemetry_value_in_bounds(0.5f, 1.0f, 0.0f));
    TEST_ASSERT_FALSE(bzm_telemetry_sample_is_within_bounds(&sample, &bounds));
    TEST_ASSERT_FALSE(bzm_telemetry_sample_is_safe(&sample, 1100, 100, &bounds, true));
}

TEST_CASE("BZM CH2 confirmation requires consecutive fresh excursions", "[asic][bzm][telemetry][qemu-integration]")
{
    bzm_telemetry_bounds_t bounds = {
        .temperature_min_c = -20.0f,
        .temperature_max_c = 105.0f,
        .ch0_min_mv = 300.0f,
        .ch0_max_mv = 800.0f,
        .ch1_min_mv = 300.0f,
        .ch1_max_mv = 800.0f,
        .ch2_abs_max_mv = 50.0f,
        .max_stack_spread_mv = 100.0f,
    };
    bzm_telemetry_store_t store;
    bzm_telemetry_store_init(&store);
    for (size_t index = 0; index < BZM_MAX_ASIC_COUNT; ++index) {
        store.samples[index].received = true;
        store.samples[index].timestamp_us = 100;
        store.samples[index].ch2_mv = 0.0f;
    }

    bzm_ch2_confirmation_t confirmation;
    bzm_ch2_confirmation_init(&confirmation);
    uint8_t culprit = 0xff;
    uint8_t observed = 0xff;
    TEST_ASSERT_EQUAL(BZM_CH2_CONFIRMATION_GOOD,
                      bzm_ch2_confirmation_observe(&confirmation, &store, &bounds, 3, &culprit, &observed));
    TEST_ASSERT_EQUAL_HEX8(0, culprit);
    TEST_ASSERT_EQUAL_UINT8(0, observed);

    for (size_t index = 0; index < BZM_MAX_ASIC_COUNT; ++index)
        store.samples[index].timestamp_us = 200;
    store.samples[1].ch2_mv = -75.0f;
    TEST_ASSERT_EQUAL(BZM_CH2_CONFIRMATION_PENDING,
                      bzm_ch2_confirmation_observe(&confirmation, &store, &bounds, 3, &culprit, &observed));
    TEST_ASSERT_EQUAL_HEX8(0x14, culprit);
    TEST_ASSERT_EQUAL_UINT8(1, observed);

    TEST_ASSERT_EQUAL(BZM_CH2_CONFIRMATION_NO_NEW_SAMPLE,
                      bzm_ch2_confirmation_observe(&confirmation, &store, &bounds, 3, &culprit, &observed));
    TEST_ASSERT_EQUAL_UINT8(1, observed);

    for (size_t index = 0; index < BZM_MAX_ASIC_COUNT; ++index)
        store.samples[index].timestamp_us = 300;
    TEST_ASSERT_EQUAL(BZM_CH2_CONFIRMATION_PENDING,
                      bzm_ch2_confirmation_observe(&confirmation, &store, &bounds, 3, &culprit, &observed));
    TEST_ASSERT_EQUAL_UINT8(2, observed);

    for (size_t index = 0; index < BZM_MAX_ASIC_COUNT; ++index)
        store.samples[index].timestamp_us = 400;
    TEST_ASSERT_EQUAL(BZM_CH2_CONFIRMATION_CONTINUOUS,
                      bzm_ch2_confirmation_observe(&confirmation, &store, &bounds, 3, &culprit, &observed));
    TEST_ASSERT_EQUAL_UINT8(3, observed);

    store.samples[1].ch2_mv = 0.0f;
    for (size_t index = 0; index < BZM_MAX_ASIC_COUNT; ++index)
        store.samples[index].timestamp_us = 500;
    TEST_ASSERT_EQUAL(BZM_CH2_CONFIRMATION_GOOD,
                      bzm_ch2_confirmation_observe(&confirmation, &store, &bounds, 3, &culprit, &observed));
}

TEST_CASE("BZM CH2 confirmation fails closed on invalid inputs", "[asic][bzm][telemetry][qemu-integration]")
{
    bzm_telemetry_bounds_t bounds = {
        .temperature_min_c = -20.0f,
        .temperature_max_c = 105.0f,
        .ch0_min_mv = 300.0f,
        .ch0_max_mv = 800.0f,
        .ch1_min_mv = 300.0f,
        .ch1_max_mv = 800.0f,
        .ch2_abs_max_mv = 50.0f,
        .max_stack_spread_mv = 100.0f,
    };
    bzm_telemetry_store_t store;
    bzm_telemetry_store_init(&store);
    bzm_ch2_confirmation_t confirmation;
    bzm_ch2_confirmation_init(&confirmation);
    TEST_ASSERT_EQUAL(BZM_CH2_CONFIRMATION_INVALID, bzm_ch2_confirmation_observe(&confirmation, &store, &bounds, 0, NULL, NULL));
    TEST_ASSERT_EQUAL(BZM_CH2_CONFIRMATION_INVALID,
                      bzm_ch2_confirmation_observe(&confirmation, &store, &bounds, BZM_CH2_CONFIRM_MAX_SAMPLES + 1U, NULL, NULL));

    for (size_t index = 0; index < BZM_MAX_ASIC_COUNT; ++index) {
        store.samples[index].received = true;
        store.samples[index].timestamp_us = 100;
        store.samples[index].ch2_mv = 0.0f;
    }
    store.samples[2].ch2_mv = NAN;
    TEST_ASSERT_EQUAL(BZM_CH2_CONFIRMATION_INVALID, bzm_ch2_confirmation_observe(&confirmation, &store, &bounds, 3, NULL, NULL));
}

static bzm_telemetry_store_t safe_confirmation_store(uint64_t timestamp_us)
{
    bzm_telemetry_store_t store;
    bzm_telemetry_store_init(&store);
    for (size_t index = 0; index < BZM_MAX_ASIC_COUNT; ++index) {
        store.samples[index] = (bzm_telemetry_sample_t){
            .asic_id = bzm_asic_wire_ids[index],
            .timestamp_us = timestamp_us,
            .received = true,
            .temperature_c = 25.0f,
            .thermal_enabled = true,
            .thermal_validity = true,
            .thermal_valid = true,
            .ch0_mv = 350.0f,
            .ch1_mv = 350.0f,
            .ch2_mv = 0.0f,
            .voltage_enabled = true,
            .voltage_valid = true,
            .pll_locked = true,
            .valid = true,
        };
    }
    return store;
}

TEST_CASE("BZM telemetry confirmation tracks transient anomalies per ASIC", "[asic][bzm][telemetry][qemu-integration]")
{
    const bzm_telemetry_bounds_t bounds = {
        .temperature_min_c = -20.0f,
        .temperature_max_c = 105.0f,
        .ch0_min_mv = 300.0f,
        .ch0_max_mv = 800.0f,
        .ch1_min_mv = 300.0f,
        .ch1_max_mv = 800.0f,
        .ch2_abs_max_mv = 50.0f,
        .max_stack_spread_mv = 100.0f,
    };
    bzm_telemetry_confirmation_t confirmation;
    bzm_telemetry_confirmation_init(&confirmation);
    uint8_t culprit = 0;
    uint8_t observed = 0;

    bzm_telemetry_store_t store = safe_confirmation_store(100);
    TEST_ASSERT_EQUAL(BZM_CH2_CONFIRMATION_GOOD,
                      bzm_telemetry_confirmation_observe(&confirmation, &store, 100, 100, &bounds, true, 3, &culprit, &observed));

    store = safe_confirmation_store(200);
    store.samples[1].ch2_mv = 637.0f;
    TEST_ASSERT_EQUAL(BZM_CH2_CONFIRMATION_PENDING,
                      bzm_telemetry_confirmation_observe(&confirmation, &store, 200, 100, &bounds, true, 3, &culprit, &observed));
    TEST_ASSERT_EQUAL_HEX8(0x14, culprit);
    TEST_ASSERT_EQUAL_UINT8(1, observed);

    store = safe_confirmation_store(300);
    store.samples[2].pll_locked = false;
    TEST_ASSERT_EQUAL(BZM_CH2_CONFIRMATION_PENDING,
                      bzm_telemetry_confirmation_observe(&confirmation, &store, 300, 100, &bounds, true, 3, &culprit, &observed));
    TEST_ASSERT_EQUAL_HEX8(0x1e, culprit);
    TEST_ASSERT_EQUAL_UINT8(1, observed);

    store = safe_confirmation_store(400);
    store.samples[0].ch2_mv = -637.0f;
    TEST_ASSERT_EQUAL(BZM_CH2_CONFIRMATION_PENDING,
                      bzm_telemetry_confirmation_observe(&confirmation, &store, 400, 100, &bounds, true, 3, &culprit, &observed));
    TEST_ASSERT_EQUAL_HEX8(0x0a, culprit);
    TEST_ASSERT_EQUAL_UINT8(1, observed);

    store = safe_confirmation_store(500);
    TEST_ASSERT_EQUAL(BZM_CH2_CONFIRMATION_GOOD,
                      bzm_telemetry_confirmation_observe(&confirmation, &store, 500, 100, &bounds, true, 3, &culprit, &observed));
}

TEST_CASE("BZM telemetry confirmation rejects a continuous same-ASIC anomaly and an immediate trip", "[asic][bzm][telemetry][qemu-integration]")
{
    const bzm_telemetry_bounds_t bounds = {
        .temperature_min_c = -20.0f,
        .temperature_max_c = 105.0f,
        .ch0_min_mv = 300.0f,
        .ch0_max_mv = 800.0f,
        .ch1_min_mv = 300.0f,
        .ch1_max_mv = 800.0f,
        .ch2_abs_max_mv = 50.0f,
        .max_stack_spread_mv = 100.0f,
    };
    bzm_telemetry_confirmation_t confirmation;
    bzm_telemetry_confirmation_init(&confirmation);
    uint8_t culprit = 0;
    uint8_t observed = 0;

    for (uint64_t timestamp = 100; timestamp <= 300; timestamp += 100) {
        bzm_telemetry_store_t store = safe_confirmation_store(timestamp);
        store.samples[3].pll_locked = false;
        bzm_ch2_confirmation_result_t result =
            bzm_telemetry_confirmation_observe(&confirmation, &store, timestamp, 100, &bounds, true, 3, &culprit, &observed);
        TEST_ASSERT_EQUAL(timestamp == 300 ? BZM_CH2_CONFIRMATION_CONTINUOUS : BZM_CH2_CONFIRMATION_PENDING, result);
    }
    TEST_ASSERT_EQUAL_HEX8(0x28, culprit);
    TEST_ASSERT_EQUAL_UINT8(3, observed);

    bzm_telemetry_confirmation_init(&confirmation);
    bzm_telemetry_store_t trip = safe_confirmation_store(400);
    trip.samples[0].trip = true;
    trip.samples[0].thermal_trip = true;
    TEST_ASSERT_EQUAL(BZM_CH2_CONFIRMATION_CONTINUOUS,
                      bzm_telemetry_confirmation_observe(&confirmation, &trip, 400, 100, &bounds, true, 3, &culprit, &observed));
    TEST_ASSERT_EQUAL_UINT8(3, observed);
}

TEST_CASE("BZM PLL telemetry confirmation recovers an isolated unlock and rejects a continuous unlock",
          "[asic][bzm][telemetry][pll][qemu-integration]")
{
    bzm_pll_lock_confirmation_t confirmation;
    bzm_pll_lock_confirmation_init(&confirmation);
    uint8_t culprit = 0;
    uint8_t observed = 0;

    bzm_telemetry_store_t store = safe_confirmation_store(100);
    TEST_ASSERT_EQUAL(BZM_CH2_CONFIRMATION_GOOD,
                      bzm_pll_lock_confirmation_observe(&confirmation, &store, 100, 100, 3, &culprit, &observed));

    store = safe_confirmation_store(200);
    store.samples[0].pll_locked = false;
    TEST_ASSERT_EQUAL(BZM_CH2_CONFIRMATION_PENDING,
                      bzm_pll_lock_confirmation_observe(&confirmation, &store, 200, 100, 3, &culprit, &observed));
    TEST_ASSERT_EQUAL_HEX8(0x0a, culprit);
    TEST_ASSERT_EQUAL_UINT8(1, observed);
    TEST_ASSERT_EQUAL(BZM_CH2_CONFIRMATION_NO_NEW_SAMPLE,
                      bzm_pll_lock_confirmation_observe(&confirmation, &store, 200, 100, 3, &culprit, &observed));

    store = safe_confirmation_store(300);
    TEST_ASSERT_EQUAL(BZM_CH2_CONFIRMATION_GOOD,
                      bzm_pll_lock_confirmation_observe(&confirmation, &store, 300, 100, 3, &culprit, &observed));
    TEST_ASSERT_EQUAL_UINT8(0, observed);

    for (uint64_t timestamp = 400; timestamp <= 600; timestamp += 100) {
        store = safe_confirmation_store(timestamp);
        store.samples[2].pll_locked = false;
        bzm_ch2_confirmation_result_t result =
            bzm_pll_lock_confirmation_observe(&confirmation, &store, timestamp, 100, 3, &culprit, &observed);
        TEST_ASSERT_EQUAL(timestamp == 600 ? BZM_CH2_CONFIRMATION_CONTINUOUS : BZM_CH2_CONFIRMATION_PENDING, result);
    }
    TEST_ASSERT_EQUAL_HEX8(0x1e, culprit);
    TEST_ASSERT_EQUAL_UINT8(3, observed);
}

TEST_CASE("BZM PLL telemetry confirmation fails closed on invalid or stale input", "[asic][bzm][telemetry][pll][qemu-integration]")
{
    bzm_pll_lock_confirmation_t confirmation;
    bzm_pll_lock_confirmation_init(&confirmation);
    bzm_telemetry_store_t store = safe_confirmation_store(100);
    TEST_ASSERT_EQUAL(BZM_CH2_CONFIRMATION_INVALID,
                      bzm_pll_lock_confirmation_observe(&confirmation, &store, 100, 100, 0, NULL, NULL));
    TEST_ASSERT_EQUAL(BZM_CH2_CONFIRMATION_INVALID,
                      bzm_pll_lock_confirmation_observe(&confirmation, &store, 201, 100, 3, NULL, NULL));
    store.samples[1].asic_id = 0x28;
    TEST_ASSERT_EQUAL(BZM_CH2_CONFIRMATION_INVALID,
                      bzm_pll_lock_confirmation_observe(&confirmation, &store, 100, 100, 3, NULL, NULL));
}

TEST_CASE("BZM telemetry store keeps independent per-ASIC samples", "[asic][bzm][telemetry][qemu-integration]")
{
    static const uint8_t payload[BZM_TDM_TELEMETRY_PAYLOAD_SIZE] = {
        0xc9, 0x00, 0x98, 0x00, 0x60, 0xd0, 0xaa, 0xe2,
    };
    bzm_telemetry_store_t store;
    bzm_telemetry_store_init(&store);

    for (size_t index = 0; index < BZM_MAX_ASIC_COUNT; ++index) {
        uint8_t id = bzm_asic_wire_ids[index];
        bzm_frame_t frame = {
            .type = BZM_FRAME_TELEMETRY,
            .asic_id = id,
            .payload_length = sizeof(payload),
            .timestamp_us = 1000 + id,
        };
        memcpy(frame.payload, payload, sizeof(payload));
        frame.payload[1] = id;
        TEST_ASSERT_TRUE(bzm_telemetry_store_apply_frame(&store, &frame));
    }

    for (size_t index = 0; index < BZM_MAX_ASIC_COUNT; ++index) {
        uint8_t id = bzm_asic_wire_ids[index];
        const bzm_telemetry_sample_t * sample = bzm_telemetry_store_get(&store, id);
        TEST_ASSERT_NOT_NULL(sample);
        TEST_ASSERT_EQUAL_HEX8(id, sample->asic_id);
        TEST_ASSERT_EQUAL_UINT32(1000 + id, (uint32_t) sample->timestamp_us);
        TEST_ASSERT_EQUAL_HEX16(0x0900 | id, sample->temperature_code);
    }
    TEST_ASSERT_NULL(bzm_telemetry_store_get(&store, 0x0b));

    bzm_frame_t result = {.type = BZM_FRAME_RESULT, .asic_id = 0x0a};
    TEST_ASSERT_FALSE(bzm_telemetry_store_apply_frame(&store, &result));
}

TEST_CASE("BZM temperature aggregation reports the hottest fresh ASIC",
          "[asic][bzm][telemetry][temperature][qemu-integration]")
{
    bzm_telemetry_store_t store;
    bzm_telemetry_store_init(&store);
    for (size_t index = 0; index < BZM_MAX_ASIC_COUNT; ++index) {
        store.samples[index].received = true;
        store.samples[index].timestamp_us = 900 + index;
        store.samples[index].temperature_c = 40.0f + 5.0f * index;
        store.samples[index].thermal_valid = true;
    }

    float hottest_c = 0.0f;
    TEST_ASSERT_TRUE(
        bzm_telemetry_max_temperature(&store, 1000, 100, &hottest_c));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 55.0f, hottest_c);

    store.samples[1].received = false;
    TEST_ASSERT_FALSE(
        bzm_telemetry_max_temperature(&store, 1000, 100, &hottest_c));
    store.samples[1].received = true;
    store.samples[1].timestamp_us = 899;
    TEST_ASSERT_FALSE(
        bzm_telemetry_max_temperature(&store, 1000, 100, &hottest_c));
    store.samples[1].timestamp_us = 901;
    store.samples[1].thermal_valid = false;
    TEST_ASSERT_FALSE(
        bzm_telemetry_max_temperature(&store, 1000, 100, &hottest_c));
    store.samples[1].thermal_valid = true;
    store.samples[1].temperature_c = NAN;
    TEST_ASSERT_FALSE(
        bzm_telemetry_max_temperature(&store, 1000, 100, &hottest_c));
    TEST_ASSERT_FALSE(
        bzm_telemetry_max_temperature(NULL, 1000, 100, &hottest_c));
    TEST_ASSERT_FALSE(
        bzm_telemetry_max_temperature(&store, 1000, 100, NULL));
}

TEST_CASE("BZM transport routes interleaved UART frames through one parser", "[asic][bzm][transport][qemu-integration]")
{
    static const uint8_t stream[] = {
        0x0a,
        BZM_FRAME_TELEMETRY,
        0xc9,
        0x00,
        0x98,
        0x00,
        0x60,
        0xd0,
        0xaa,
        0xe2,
        0x1e,
        BZM_FRAME_RESULT,
        0x83,
        0x45,
        0x78,
        0x56,
        0x34,
        0x12,
        0x17,
        0x0d,
        0x28,
        BZM_FRAME_NOOP,
        '2',
        'Z',
        'B',
        0x14,
        BZM_FRAME_REGISTER,
        0xde,
        0xad,
        0xbe,
        0xef,
    };
    static const uint8_t expected_register[] = {0xde, 0xad, 0xbe, 0xef};
    bzm_serial_transport_t * transport = calloc(1, sizeof(*transport));
    TEST_ASSERT_NOT_NULL(transport);
    TEST_ASSERT_TRUE(bzm_serial_transport_init(transport));
    TEST_ASSERT_TRUE(bzm_serial_expect_register_reply(transport, 0x14, sizeof(expected_register)));

    size_t emitted = 0;
    for (size_t i = 0; i < sizeof(stream); ++i) {
        emitted += bzm_serial_transport_ingest_bytes(transport, stream + i, 1, 1000 + i);
    }
    TEST_ASSERT_EQUAL_UINT32(4, emitted);

    bzm_serial_parser_stats_t stats;
    TEST_ASSERT_TRUE(bzm_serial_get_parser_stats(transport, &stats));
    TEST_ASSERT_EQUAL_UINT32(4, stats.emitted_frames);
    TEST_ASSERT_EQUAL_UINT32(1, transport->io.pending_result_length);
    TEST_ASSERT_FALSE(transport->io.noop_ready);
    TEST_ASSERT_EQUAL_UINT32(0, stats.discarded_bytes);

    bzm_raw_result_t result;
    TEST_ASSERT_TRUE(bzm_serial_read_result(transport, &result, 0));
    TEST_ASSERT_EQUAL_HEX8(0x1e, result.asic_id);
    TEST_ASSERT_EQUAL_HEX16(0x0345, result.engine_id);
    TEST_ASSERT_EQUAL_HEX32(0x12345678, result.nonce);
    TEST_ASSERT_EQUAL_HEX8(0x17, result.sequence_id);

    uint8_t register_data[sizeof(expected_register)];
    TEST_ASSERT_FALSE(bzm_serial_take_register_reply(transport, 0x14, register_data, 2));
    TEST_ASSERT_TRUE(bzm_serial_take_register_reply(transport, 0x14, register_data, sizeof(register_data)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_register, register_data, sizeof(register_data));

    bzm_telemetry_sample_t sample;
    TEST_ASSERT_TRUE(bzm_serial_get_telemetry(transport, 0x0a, &sample));
    TEST_ASSERT_TRUE(sample.valid);
    TEST_ASSERT_TRUE(sample.pll_locked);
    TEST_ASSERT_FALSE(bzm_serial_get_telemetry(transport, 0x14, &sample));

    bzm_telemetry_store_t snapshot;
    TEST_ASSERT_TRUE(bzm_serial_get_telemetry_snapshot(transport, &snapshot));
    const bzm_telemetry_sample_t * stored = bzm_telemetry_store_get(&snapshot, 0x0a);
    TEST_ASSERT_NOT_NULL(stored);
    TEST_ASSERT_TRUE(stored->received);

    TEST_ASSERT_FALSE(bzm_serial_read_result(transport, &result, 0));
    bzm_serial_transport_deinit(transport);
    free(transport);
}

TEST_CASE("BZM serial poll drains a complete high-rate TDM backlog", "[asic][bzm][transport][tdm][qemu-integration]")
{
    static const uint8_t payload[BZM_TDM_TELEMETRY_PAYLOAD_SIZE] = {
        0xc9, 0x00, 0x98, 0x00, 0x60, 0xd0, 0xaa, 0xe2,
    };
    serial_reader_mock_t reader = {0};
    for (size_t frame = 0; frame < 100; ++frame) {
        reader.bytes[reader.length++] = bzm_asic_wire_ids[frame % BZM_MAX_ASIC_COUNT];
        reader.bytes[reader.length++] = BZM_FRAME_TELEMETRY;
        memcpy(reader.bytes + reader.length, payload, sizeof(payload));
        reader.length += sizeof(payload);
    }

    bzm_serial_transport_t * transport = calloc(1, sizeof(*transport));
    TEST_ASSERT_NOT_NULL(transport);
    TEST_ASSERT_TRUE(bzm_serial_transport_init(transport));
    TEST_ASSERT_EQUAL_UINT32(100, bzm_serial_poll_with_reader(transport, 25, serial_reader_mock, &reader));
    TEST_ASSERT_EQUAL_UINT32(reader.length, reader.cursor);
    TEST_ASSERT_GREATER_THAN_UINT32(2, reader.calls);

    bzm_serial_parser_stats_t stats;
    TEST_ASSERT_TRUE(bzm_serial_get_parser_stats(transport, &stats));
    TEST_ASSERT_EQUAL_UINT32(100, stats.emitted_frames);
    TEST_ASSERT_EQUAL_UINT32(0, stats.discarded_bytes);
    TEST_ASSERT_EQUAL_UINT32(0, stats.buffered_bytes);
    for (size_t index = 0; index < BZM_MAX_ASIC_COUNT; ++index) {
        bzm_telemetry_sample_t sample;
        TEST_ASSERT_TRUE(bzm_serial_get_telemetry(transport, bzm_asic_wire_ids[index], &sample));
        TEST_ASSERT_TRUE(sample.valid);
    }
    bzm_serial_transport_deinit(transport);
    free(transport);
}

TEST_CASE("BZM transport register reservations reject ambiguity and recover framing", "[asic][bzm][transport][qemu-integration]")
{
    static const uint8_t stream[] = {
        0x14, BZM_FRAME_REGISTER, 0xaa, 0xbb, 0x0a, BZM_FRAME_REGISTER, 0x11, 0x22,
    };
    bzm_serial_transport_t * transport = calloc(1, sizeof(*transport));
    TEST_ASSERT_NOT_NULL(transport);
    TEST_ASSERT_TRUE(bzm_serial_transport_init(transport));
    TEST_ASSERT_TRUE(bzm_serial_expect_register_reply(transport, 0x0a, 2));
    TEST_ASSERT_FALSE(bzm_serial_expect_register_reply(transport, 0x14, 2));

    TEST_ASSERT_EQUAL_UINT32(1, bzm_serial_transport_ingest_bytes(transport, stream, sizeof(stream), 2000));
    uint8_t response[2];
    TEST_ASSERT_TRUE(bzm_serial_take_register_reply(transport, 0x0a, response, sizeof(response)));
    TEST_ASSERT_EQUAL_HEX8(0x11, response[0]);
    TEST_ASSERT_EQUAL_HEX8(0x22, response[1]);
    TEST_ASSERT_FALSE(bzm_serial_cancel_register_reply(transport, 0x0a));

    bzm_serial_parser_stats_t stats;
    TEST_ASSERT_TRUE(bzm_serial_get_parser_stats(transport, &stats));
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(4, stats.discarded_bytes);

    TEST_ASSERT_TRUE(bzm_serial_expect_register_reply(transport, 0x28, 4));
    TEST_ASSERT_TRUE(bzm_serial_cancel_register_reply(transport, 0x28));
    TEST_ASSERT_FALSE(bzm_serial_cancel_register_reply(transport, 0x28));
    bzm_serial_transport_deinit(transport);
    free(transport);
}
