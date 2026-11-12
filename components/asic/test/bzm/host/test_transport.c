#include "check.h"
#include "bzm/transport.h"
#include "bzm/telemetry_decode.h"
#include "bzm/work_registers.h"
#include <string.h>

typedef struct {
    uint8_t rx[2048], tx[8192];
    size_t length, cursor, written, chunk, reads, clears, lock_count, io_errors;
    uint64_t now;
    bool locked, short_write, read_error, error_after_data, overlong_read;
} fixture_t;

static int16_t read_bytes(void *context, uint8_t *data, uint16_t size, uint16_t timeout_ms)
{
    fixture_t *f = context;
    CHECK(!f->locked);
    ++f->reads;
    if (f->read_error || (f->error_after_data && f->cursor == f->length)) return -1;
    if (f->overlong_read) return (int16_t)(size + 1);
    size_t count = f->length - f->cursor;
    if (count > size) count = size;
    if (count > f->chunk) count = f->chunk;
    memcpy(data, f->rx + f->cursor, count);
    f->cursor += count;
    f->now += count ? 1 : (uint64_t)timeout_ms * 1000;
    return (int16_t)count;
}

static int write_bytes(void *context, const uint8_t *data, size_t size)
{
    fixture_t *f = context;
    CHECK(!f->locked);
    CHECK(f->written + size <= sizeof(f->tx));
    memcpy(f->tx + f->written, data, size);
    f->written += size;
    return (int)(size - (f->short_write ? 1 : 0));
}

static void clear_rx(void *context)
{
    fixture_t *f = context;
    CHECK(!f->locked);
    f->cursor = f->length;
    ++f->clears;
}
static uint64_t now_us(void *context) { return ((fixture_t *)context)->now; }
static void io_error(void *context)
{
    fixture_t *f = context;
    CHECK(!f->locked);
    ++f->io_errors;
}
static void lock(void *context)
{
    fixture_t *f = context;
    CHECK(!f->locked);
    f->locked = true;
    ++f->lock_count;
}
static void unlock(void *context)
{
    fixture_t *f = context;
    CHECK(f->locked);
    f->locked = false;
}
static const bzm_serial_ops_t OPS = {
    .read = read_bytes, .write = write_bytes, .clear_rx = clear_rx,
    .now_us = now_us, .lock = lock, .unlock = unlock,
    .on_io_error = io_error,
};

static void init(bzm_serial_transport_t *t, fixture_t *f)
{
    *t = (bzm_serial_transport_t){0};
    *f = (fixture_t){.now = 1000000, .chunk = 3};
    CHECK(bzm_serial_transport_init(t, &OPS, f));
}
static void feed(fixture_t *f, const uint8_t *bytes, size_t length)
{
    CHECK(f->length + length <= sizeof(f->rx));
    memcpy(f->rx + f->length, bytes, length);
    f->length += length;
}

static void test_two_devices(void)
{
    bzm_serial_transport_t a, b;
    fixture_t first, second;
    init(&a, &first);
    init(&b, &second);
    const uint8_t noop[] = {0x0a, 0x0f, '2', 'Z', 'B'};
    const uint8_t expected[] = {0x0a, 1, 0xf0, 0};
    feed(&first, noop, sizeof(noop));
    CHECK(bzm_serial_probe_noop(&a, 0x0a) == BZM_SERIAL_PROBE_RESPONSE);
    CHECK(first.written == sizeof(expected));
    CHECK(memcmp(first.tx, expected, sizeof(expected)) == 0);
    CHECK(second.written == 0 && second.reads == 0);
    CHECK(bzm_serial_probe_noop(&b, 0x14) == BZM_SERIAL_PROBE_NO_RESPONSE);
    CHECK(second.now == 1150000);
    CHECK(first.lock_count > 0 && !first.locked && !second.locked);
    bzm_serial_transport_deinit(&a);
    CHECK(!bzm_serial_write_register_to(&a, 0x0a, 0, 0, noop, 1));
    CHECK(bzm_serial_transport_init(&a, &OPS, &first));
    bzm_serial_transport_deinit(&a);
    bzm_serial_transport_deinit(&b);
}

static void test_interleaved_replies(void)
{
    bzm_serial_transport_t t;
    fixture_t f;
    init(&t, &f);
    f.chunk = 1;
    const uint8_t stream[] = {
        0x0a, 0x0d, 0xc9, 0x00, 0x98, 0x00, 0x60, 0xd0, 0xaa, 0xe2,
        0x1e, 0x01, 0x83, 0x45, 0x78, 0x56, 0x34, 0x12, 0x17, 0x0d,
        0x14, 0x03, 0xde, 0xad, 0xbe, 0xef,
    };
    const uint8_t request[] = {0x14, 1, 0x3f, 0, 0xff, 0, 0x22, 0, 3, 0, 0, 0};
    feed(&f, stream, sizeof(stream));
    uint8_t data[4];
    CHECK(bzm_serial_read_register(&t, 0x14, 0xfff, 0x22, data, sizeof(data)));
    CHECK(memcmp(data, "\xde\xad\xbe\xef", 4) == 0);
    CHECK(f.written == sizeof(request) && memcmp(f.tx, request, sizeof(request)) == 0);
    bzm_raw_result_t result;
    CHECK(bzm_serial_read_result(&t, &result, 0));
    CHECK(result.asic_id == 0x1e && result.nonce == 0x12345678 && result.engine_id == 0x345);
    CHECK(!bzm_serial_read_result(&t, &result, 0));
    bzm_telemetry_sample_t sample;
    CHECK(bzm_serial_get_telemetry(&t, 0x0a, &sample));
    CHECK(sample.valid && sample.pll_locked && sample.timestamp_us > 1000000);
    bzm_serial_parser_stats_t stats;
    CHECK(bzm_serial_get_parser_stats(&t, &stats));
    CHECK(stats.emitted_frames == 3 && stats.discarded_bytes == 0 && stats.buffered_bytes == 0);
    bzm_serial_transport_deinit(&t);
}

static void test_errors_and_recovery(void)
{
    bzm_serial_transport_t t;
    fixture_t f;
    init(&t, &f);
    f.short_write = true;
    CHECK(bzm_serial_probe_noop(&t, 0x0a) == BZM_SERIAL_PROBE_IO_ERROR);
    CHECK(f.clears == 1 && !t.io.noop_pending);
    uint8_t data[4];
    CHECK(!bzm_serial_read_register(&t, 0x0a, 0xfff, 0, data, 4));
    CHECK(!t.io.register_pending);
    f.short_write = false;
    f.read_error = true;
    size_t reads_before = f.reads;
    CHECK(bzm_serial_probe_noop(&t, 0x0a) == BZM_SERIAL_PROBE_IO_ERROR);
    CHECK(f.reads == reads_before + 1);
    CHECK(f.io_errors == 1);
    f.read_error = false;
    const uint8_t partial[] = {0x0a, 0x03, 0x11};
    feed(&f, partial, sizeof(partial));
    uint64_t started = f.now;
    CHECK(!bzm_serial_read_register(&t, 0x0a, 0xfff, 0, data, 4));
    CHECK(f.now - started >= 1000000 && f.now - started < 1001000);
    CHECK(!t.io.register_pending && t.io.parser.buffered_length == 0);
    const uint8_t reply[] = {0x0a, 0x03, 1, 2, 3, 4};
    feed(&f, reply, sizeof(reply));
    CHECK(bzm_serial_read_register(&t, 0x0a, 0xfff, 0, data, 4));
    CHECK(memcmp(data, "\x01\x02\x03\x04", 4) == 0);
    bzm_serial_transport_deinit(&t);
}

static void test_poll_results(void)
{
    bzm_serial_transport_t t;
    fixture_t f;
    init(&t, &f);
    CHECK(bzm_serial_poll(NULL, 1).status == BZM_ERR_INVALID_ARG);
    bzm_serial_poll_result_t poll = bzm_serial_poll(&t, 0);
    CHECK(poll.status == BZM_OK && poll.bytes_received == 0 && poll.frames_emitted == 0);
    CHECK(f.reads == 1 && f.now == 1000000);
    poll = bzm_serial_poll(&t, 1);
    CHECK(poll.status == BZM_OK && poll.bytes_received == 0 && poll.frames_emitted == 0);
    const uint8_t partial[] = {0x0a, 0x0f};
    feed(&f, partial, sizeof(partial));
    poll = bzm_serial_poll(&t, 0);
    CHECK(poll.status == BZM_OK && poll.bytes_received == 2 && poll.frames_emitted == 0);
    CHECK(t.io.parser.buffered_length == 2 && f.io_errors == 0);
    const uint8_t payload[] = {'2', 'Z', 'B'};
    feed(&f, payload, sizeof(payload));
    poll = bzm_serial_poll(&t, 0);
    CHECK(poll.status == BZM_OK && poll.bytes_received == 3 && poll.frames_emitted == 1);
    CHECK(t.io.parser.buffered_length == 0 && f.io_errors == 0);
    const uint8_t noop[] = {0x0a, 0x0f, '2', 'Z', 'B'};
    feed(&f, noop, sizeof(noop));
    f.error_after_data = true;
    poll = bzm_serial_poll(&t, 0);
    CHECK(poll.status == BZM_ERR_IO && poll.bytes_received == 5 && poll.frames_emitted == 1);
    CHECK(f.io_errors == 1);
    f.error_after_data = false;
    f.overlong_read = true;
    poll = bzm_serial_poll(&t, 1);
    CHECK(poll.status == BZM_ERR_IO && poll.bytes_received == 0 && poll.frames_emitted == 0);
    CHECK(f.io_errors == 2);
    t.ops.on_io_error = NULL;
    CHECK(bzm_serial_poll(&t, 1).status == BZM_ERR_IO);
    CHECK(f.io_errors == 2);
    bzm_serial_transport_deinit(&t);
    CHECK(bzm_serial_poll(&t, 1).status == BZM_ERR_INVALID_STATE);
}

typedef struct { uint8_t data[256][32]; size_t writes; } register_capture_t;
static bool capture_register(void *context, uint16_t engine, uint8_t offset, const void *data, size_t length)
{
    register_capture_t *capture = context;
    CHECK(engine == 0x345 && length <= 32);
    memcpy(capture->data[offset], data, length);
    ++capture->writes;
    return true;
}

static void test_work_wire_bytes(void)
{
    register_capture_t capture = {0};
    bzm_work_t work = {
        .engine_id = 0x345, .starting_nonce = 0x12345678, .end_nonce = 0x90abcdef,
        .merkle_residue = 0xaabbccdd, .start_ntime = 0x11223344, .target = 0x1d00ffff,
        .lead_zeros = 36, .timestamp_count = 60, .logical_sequence = 7,
    };
    memset(work.midstates[3], 0x5a, 32);
    CHECK(bzm_transport_program_work(&work, capture_register, &capture));
    CHECK(capture.writes == 16);
    CHECK(memcmp(capture.data[BZM_REG_START_NONCE], "\x78\x56\x34\x12", 4) == 0);
    CHECK(memcmp(capture.data[BZM_REG_END_NONCE], "\xef\xcd\xab\x90", 4) == 0);
    CHECK(memcmp(capture.data[BZM_REG_MERKLE_RESIDUE], "\xaa\xbb\xcc\xdd", 4) == 0);
    CHECK(memcmp(capture.data[BZM_REG_START_TIMESTAMP], "\x11\x22\x33\x44", 4) == 0);
    CHECK(memcmp(capture.data[BZM_REG_TARGET], "\x1d\x00\xff\xff", 4) == 0);
    CHECK(memcmp(capture.data[BZM_REG_MIDSTATE], work.midstates[3], 32) == 0);
    CHECK(capture.data[BZM_REG_ZEROS_TO_FIND][0] == 4);
    CHECK(capture.data[BZM_REG_SEQUENCE_ID][0] == 31);
    CHECK(capture.data[BZM_REG_JOB_CONTROL][0] == 3);
    uint32_t first, last;
    CHECK(bzm_partition_nonce_range(0, UINT32_MAX, 3, 4, &first, &last));
    CHECK(first == 0xc0000000 && last == UINT32_MAX);
}

static void test_adapter_contract(void)
{
    bzm_serial_transport_t t = {0};
    bzm_serial_ops_t ops = OPS;
    fixture_t f = {.chunk = 1};
    CHECK(!bzm_serial_transport_init(NULL, &ops, &f));
    CHECK(!bzm_serial_transport_init(&t, NULL, &f));
    ops.now_us = NULL;
    CHECK(!bzm_serial_transport_init(&t, &ops, &f));
    ops = OPS;
    ops.unlock = NULL;
    CHECK(!bzm_serial_transport_init(&t, &ops, &f));
    ops.lock = NULL;
    CHECK(bzm_serial_transport_init(&t, &ops, &f));
    CHECK(!bzm_serial_transport_init(&t, &ops, &f));
    CHECK(bzm_serial_probe_noop(&t, 0x0a) == BZM_SERIAL_PROBE_NO_RESPONSE);
    bzm_serial_transport_deinit(&t);
}

static void test_runtime_preserves_nonce_quarters(void)
{
    bzm_serial_transport_t t;
    fixture_t f;
    init(&t, &f);
    t.asic_count = BZM_MAX_ASIC_COUNT;
    t.engine_count = 1;
    bzm_work_t work = {.engine_id = 0, .end_nonce = UINT32_MAX, .lead_zeros = 36};
    for (size_t i = 0; i < 2; ++i) {
        size_t before = f.written;
        CHECK(bzm_serial_write_work(&t, &work));
        CHECK(f.written - before == 462);
        CHECK(bzm_serial_flush(&t));
    }
    // Decode every actual UART write, including the sequence-reuse flush: only common fields may change.
    for (size_t cursor = 0; cursor < f.written;) {
        CHECK(f.tx[cursor] == BZM_ALL_ASICS && f.tx[cursor + 1] == 1);
        uint8_t offset = f.tx[cursor + 6];
        CHECK(offset != BZM_REG_START_NONCE && offset != BZM_REG_END_NONCE);
        size_t length = f.tx[cursor + 8] + 1U;
        cursor += (6 + length) * 2;
        CHECK(cursor <= f.written);
    }
    size_t before = f.written;
    work.starting_nonce = 1;
    CHECK(!bzm_serial_write_work(&t, &work));
    CHECK(f.written == before); // A partial-domain request cannot silently reuse full-domain quarters.
    bzm_serial_transport_deinit(&t);
}

static void test_multicast_encoding(void)
{
    const uint8_t value = 3;
    uint8_t encoded[14];
    const uint8_t expected[] = {0xff, 1, 0x40, 0, 19, 0, 0x39, 0, 0, 0, 3, 0, 0, 0};
    CHECK(bzm_transport_encode_multicast(BZM_ALL_ASICS, 19, BZM_REG_JOB_CONTROL,
                                         &value, 1, encoded, sizeof(encoded)) == sizeof(expected));
    CHECK(memcmp(encoded, expected, sizeof(expected)) == 0);
    CHECK(bzm_transport_encode_multicast(BZM_ALL_ASICS, 0xff, BZM_REG_JOB_CONTROL,
                                         &value, 1, encoded, sizeof(encoded)) == sizeof(expected));
    CHECK(encoded[4] == 0xff);
    CHECK(bzm_transport_encode_multicast(BZM_ALL_ASICS, BZM_ENGINE_ROWS, BZM_REG_JOB_CONTROL,
                                         &value, 1, encoded, sizeof(encoded)) == 0);
    CHECK(bzm_transport_encode_multicast(BZM_ALL_ASICS, 0, BZM_REG_JOB_CONTROL,
                                         &value, 1, encoded, sizeof(encoded) - 1) == 0);
}

int main(void)
{
    test_adapter_contract();
    test_two_devices();
    test_interleaved_replies();
    test_errors_and_recovery();
    test_poll_results();
    test_work_wire_bytes();
    test_runtime_preserves_nonce_quarters();
    test_multicast_encoding();
    puts("BZM host transport: 8 scenarios passed");
    return 0;
}
