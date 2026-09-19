#include "bzm_transport.h"
#include "bzm_work_registers.h"

#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bzm_serial.h"

enum { BZM_REGISTER_REPLY_TIMEOUT_US = 1000000 };

typedef struct
{
    uint8_t asic_id;
} serial_register_context_t;

static bool addressed_asic(uint8_t asic_id)
{
    return bzm_topology_asic_index(asic_id, NULL);
}

static void queue_result_locked(bzm_serial_transport_t * transport, const bzm_raw_result_t * result)
{
    bzm_serial_io_state_t * io = &transport->io;
    if (io->pending_result_length == BZM_PENDING_RESULT_COUNT) {
        io->pending_result_head = (io->pending_result_head + 1) % BZM_PENDING_RESULT_COUNT;
        io->pending_result_length--;
    }
    size_t tail = (io->pending_result_head + io->pending_result_length) % BZM_PENDING_RESULT_COUNT;
    io->pending_results[tail] = *result;
    io->pending_result_length++;
}

static void transport_frame_handler(void * context, const bzm_frame_t * frame)
{
    bzm_serial_transport_t * transport = context;
    bzm_serial_io_state_t * io = &transport->io;

    switch (frame->type) {
    case BZM_FRAME_RESULT: {
        bzm_raw_result_t result;
        if (!addressed_asic(frame->asic_id) || !bzm_result_decode(frame->payload, frame->timestamp_us, &result)) {
            break;
        }
        result.asic_id = frame->asic_id;
        queue_result_locked(transport, &result);
        break;
    }
    case BZM_FRAME_REGISTER:
        if (!io->register_pending || frame->asic_id != io->register_asic_id || frame->payload_length != io->register_length) {
            break;
        }
        memcpy(io->register_data, frame->payload, frame->payload_length);
        io->register_ready = true;
        break;
    case BZM_FRAME_TELEMETRY:
        (void)bzm_telemetry_store_apply_frame(&io->telemetry, frame);
        break;
    case BZM_FRAME_NOOP:
        if (frame->payload_length != BZM_TDM_NOOP_PAYLOAD_SIZE || memcmp(frame->payload, "2ZB", BZM_TDM_NOOP_PAYLOAD_SIZE) != 0) {
            io->invalid_noop_frames++;
            break;
        }
        if (!io->noop_pending || (io->noop_asic_id != BZM_BROADCAST_ASIC && io->noop_asic_id != frame->asic_id)) {
            break;
        }
        io->noop_ready = true;
        break;
    }
}

bool bzm_serial_transport_init(bzm_serial_transport_t * transport)
{
    if (transport == NULL)
        return false;
    if (transport->io.initialized)
        return true;

    memset(&transport->io, 0, sizeof(transport->io));
    if (pthread_mutex_init(&transport->io.lock, NULL) != 0)
        return false;
    transport->io.initialized = true;
    bzm_frame_parser_init(&transport->io.parser, transport_frame_handler, transport);
    bzm_telemetry_store_init(&transport->io.telemetry);
    return true;
}

void bzm_serial_transport_deinit(bzm_serial_transport_t * transport)
{
    if (transport == NULL || !transport->io.initialized)
        return;
    pthread_mutex_destroy(&transport->io.lock);
    memset(&transport->io, 0, sizeof(transport->io));
}

static void reset_io_state(bzm_serial_transport_t * transport, bool reset_telemetry, bool reset_counters)
{
    bzm_serial_io_state_t * io = &transport->io;
    pthread_mutex_lock(&io->lock);
    io->pending_result_head = 0;
    io->pending_result_length = 0;
    io->register_pending = false;
    io->register_ready = false;
    io->noop_pending = false;
    io->noop_ready = false;
    io->parser.buffered_length = 0;
    memset(io->parser.expected_register_length, 0, sizeof(io->parser.expected_register_length));
    if (reset_telemetry)
        bzm_telemetry_store_init(&io->telemetry);
    if (reset_counters) {
        io->parser.emitted_frames = 0;
        io->parser.discarded_bytes = 0;
        io->invalid_noop_frames = 0;
    }
    pthread_mutex_unlock(&io->lock);
}

size_t bzm_serial_transport_ingest_bytes(bzm_serial_transport_t * transport, const uint8_t * data, size_t data_length,
                                         uint64_t timestamp_us)
{
    if (transport == NULL || !transport->io.initialized || (data == NULL && data_length != 0)) {
        return 0;
    }
    pthread_mutex_lock(&transport->io.lock);
    size_t emitted = bzm_frame_parser_feed(&transport->io.parser, data, data_length, timestamp_us);
    pthread_mutex_unlock(&transport->io.lock);
    return emitted;
}

static bool append_word(uint16_t word, uint8_t * encoded, size_t capacity, size_t * offset)
{
    if (*offset + 2 > capacity)
        return false;
    encoded[(*offset)++] = word & 0xff;
    encoded[(*offset)++] = (word >> 8) & 0x01;
    return true;
}

size_t bzm_transport_encode_write(uint8_t asic_id, uint16_t engine_id, uint8_t offset, const uint8_t * data, size_t data_len,
                                  uint8_t * encoded, size_t encoded_capacity)
{
    if (data == NULL || data_len == 0 || data_len > 256 || encoded == NULL || engine_id >= BZM_MAX_ENGINE_COUNT) {
        return 0;
    }

    size_t cursor = 0;
    if (!append_word(0x100 | asic_id, encoded, encoded_capacity, &cursor) ||
        !append_word(0x020 | ((engine_id >> 8) & 0x0f), encoded, encoded_capacity, &cursor) ||
        !append_word(engine_id & 0xff, encoded, encoded_capacity, &cursor) ||
        !append_word(offset, encoded, encoded_capacity, &cursor) ||
        !append_word(data_len - 1, encoded, encoded_capacity, &cursor)) {
        return 0;
    }
    for (size_t i = 0; i < data_len; ++i) {
        if (!append_word(data[i], encoded, encoded_capacity, &cursor)) {
            return 0;
        }
    }
    return append_word(0, encoded, encoded_capacity, &cursor) ? cursor : 0;
}

size_t bzm_transport_encode_read(uint8_t asic_id, uint16_t engine_id, uint8_t offset, size_t data_len, uint8_t * encoded,
                                 size_t encoded_capacity)
{
    if (data_len == 0 || data_len > 256 || encoded == NULL || engine_id >= BZM_MAX_ENGINE_COUNT) {
        return 0;
    }

    size_t cursor = 0;
    return append_word(0x100 | asic_id, encoded, encoded_capacity, &cursor) &&
                   append_word(0x030 | ((engine_id >> 8) & 0x0f), encoded, encoded_capacity, &cursor) &&
                   append_word(engine_id & 0xff, encoded, encoded_capacity, &cursor) &&
                   append_word(offset, encoded, encoded_capacity, &cursor) &&
                   append_word(data_len - 1, encoded, encoded_capacity, &cursor) &&
                   append_word(0, encoded, encoded_capacity, &cursor)
               ? cursor
               : 0;
}

size_t bzm_transport_encode_noop(uint8_t asic_id, uint8_t * encoded, size_t encoded_capacity)
{
    if (encoded == NULL)
        return 0;
    size_t cursor = 0;
    return append_word(0x100 | asic_id, encoded, encoded_capacity, &cursor) &&
                   append_word(0x0f0, encoded, encoded_capacity, &cursor)
               ? cursor
               : 0;
}

bool bzm_partition_nonce_range(uint32_t starting_nonce, uint32_t end_nonce, size_t partition, size_t partition_count,
                               uint32_t * partition_start, uint32_t * partition_end)
{
    if (partition_count == 0 || partition >= partition_count || partition_start == NULL || partition_end == NULL ||
        end_nonce < starting_nonce) {
        return false;
    }
    uint64_t nonce_count = (uint64_t) end_nonce - starting_nonce + 1;
    uint64_t nonce_step = nonce_count / partition_count;
    if (nonce_step == 0)
        return false;
    *partition_start = starting_nonce + nonce_step * partition;
    *partition_end = partition + 1 == partition_count ? end_nonce : starting_nonce + nonce_step * (partition + 1) - 1;
    return true;
}

static bool serial_write_register_raw(uint8_t asic_id, uint16_t engine_id, uint8_t offset, const void * data, size_t data_len)
{
    uint8_t encoded[(32 + 6) * 2];
    if (data_len > 32)
        return false;
    size_t length = bzm_transport_encode_write(asic_id, engine_id, offset, data, data_len, encoded, sizeof(encoded));
    return length != 0 && BZM_SERIAL_send(encoded, (int)length, false) == (int)length;
}

bool bzm_serial_write_register_to(bzm_serial_transport_t * transport, uint8_t asic_id, uint16_t engine_id, uint8_t offset,
                                  const void * data, size_t data_len)
{
    if (transport == NULL)
        return false;
    return serial_write_register_raw(asic_id, engine_id, offset, data, data_len);
}

static bool dequeue_result(bzm_serial_transport_t * transport, bzm_raw_result_t * result)
{
    if (transport == NULL || !transport->io.initialized || result == NULL)
        return false;

    bzm_serial_io_state_t * io = &transport->io;
    pthread_mutex_lock(&io->lock);
    bool available = io->pending_result_length != 0;
    if (available) {
        *result = io->pending_results[io->pending_result_head];
        io->pending_result_head = (io->pending_result_head + 1) % BZM_PENDING_RESULT_COUNT;
        io->pending_result_length--;
    }
    pthread_mutex_unlock(&io->lock);
    return available;
}

void bzm_serial_discard_pending_results(bzm_serial_transport_t *transport)
{
    if (transport == NULL || !transport->io.initialized)
        return;
    pthread_mutex_lock(&transport->io.lock);
    transport->io.pending_result_head = 0;
    transport->io.pending_result_length = 0;
    pthread_mutex_unlock(&transport->io.lock);
}

bool bzm_serial_expect_register_reply(bzm_serial_transport_t * transport, uint8_t asic_id, size_t payload_length)
{
    if (transport == NULL || !transport->io.initialized || !addressed_asic(asic_id) || payload_length == 0 ||
        payload_length > BZM_TDM_MAX_REGISTER_PAYLOAD_SIZE) {
        return false;
    }

    bzm_serial_io_state_t * io = &transport->io;
    pthread_mutex_lock(&io->lock);
    bool accepted = !io->register_pending && bzm_frame_parser_expect_register(&io->parser, asic_id, payload_length);
    if (accepted) {
        io->register_pending = true;
        io->register_ready = false;
        io->register_asic_id = asic_id;
        io->register_length = payload_length;
    }
    pthread_mutex_unlock(&io->lock);
    return accepted;
}

bool bzm_serial_take_register_reply(bzm_serial_transport_t * transport, uint8_t asic_id, void * data, size_t data_length)
{
    if (transport == NULL || !transport->io.initialized || data == NULL)
        return false;

    bzm_serial_io_state_t * io = &transport->io;
    pthread_mutex_lock(&io->lock);
    bool ready =
        io->register_pending && io->register_ready && io->register_asic_id == asic_id && io->register_length == data_length;
    if (ready) {
        memcpy(data, io->register_data, data_length);
        io->register_pending = false;
        io->register_ready = false;
        io->register_length = 0;
    }
    pthread_mutex_unlock(&io->lock);
    return ready;
}

bool bzm_serial_cancel_register_reply(bzm_serial_transport_t * transport, uint8_t asic_id)
{
    if (transport == NULL || !transport->io.initialized)
        return false;

    bzm_serial_io_state_t * io = &transport->io;
    pthread_mutex_lock(&io->lock);
    bool pending = io->register_pending && io->register_asic_id == asic_id;
    if (pending) {
        (void) bzm_frame_parser_cancel_register(&io->parser, asic_id);
        io->register_pending = false;
        io->register_ready = false;
        io->register_length = 0;
    }
    pthread_mutex_unlock(&io->lock);
    return pending;
}

bool bzm_serial_get_telemetry(bzm_serial_transport_t * transport, uint8_t asic_id, bzm_telemetry_sample_t * sample)
{
    if (transport == NULL || !transport->io.initialized || sample == NULL || !addressed_asic(asic_id)) {
        return false;
    }

    pthread_mutex_lock(&transport->io.lock);
    const bzm_telemetry_sample_t * stored = bzm_telemetry_store_get(&transport->io.telemetry, asic_id);
    bool received = stored != NULL && stored->received;
    if (stored != NULL)
        *sample = *stored;
    pthread_mutex_unlock(&transport->io.lock);
    return received;
}

bool bzm_serial_get_telemetry_snapshot(bzm_serial_transport_t * transport, bzm_telemetry_store_t * snapshot)
{
    if (transport == NULL || !transport->io.initialized || snapshot == NULL)
        return false;
    pthread_mutex_lock(&transport->io.lock);
    *snapshot = transport->io.telemetry;
    pthread_mutex_unlock(&transport->io.lock);
    return true;
}

bool bzm_serial_get_parser_stats(bzm_serial_transport_t * transport, bzm_serial_parser_stats_t * stats)
{
    if (transport == NULL || !transport->io.initialized || stats == NULL)
        return false;

    bzm_serial_io_state_t * io = &transport->io;
    pthread_mutex_lock(&io->lock);
    *stats = (bzm_serial_parser_stats_t){
        .emitted_frames = io->parser.emitted_frames,
        .discarded_bytes = io->parser.discarded_bytes,
        .invalid_noop_frames = io->invalid_noop_frames,
        .buffered_bytes = io->parser.buffered_length,
    };
    pthread_mutex_unlock(&io->lock);
    return true;
}

size_t bzm_serial_poll(bzm_serial_transport_t * transport, uint16_t timeout_ms)
{
    enum {
        BZM_POLL_BUFFER_SIZE = 128U,
        BZM_POLL_MAX_DRAIN_BYTES = BZM_SERIAL_RX_BUFFER_BYTES,
    };
    if (transport == NULL || !transport->io.initialized || timeout_ms == 0)
        return 0;

    uint8_t received[BZM_POLL_BUFFER_SIZE];
    int16_t length = BZM_SERIAL_rx(received, 1, timeout_ms);
    size_t total_bytes = 0;
    size_t emitted = 0;
    while (length > 0 && total_bytes < BZM_POLL_MAX_DRAIN_BYTES) {
        size_t accepted = (size_t)length;
        if (accepted > sizeof(received) ||
            accepted > BZM_POLL_MAX_DRAIN_BYTES - total_bytes) break;
        emitted += bzm_serial_transport_ingest_bytes(
            transport, received, accepted, (uint64_t)esp_timer_get_time());
        total_bytes += accepted;
        uint16_t capacity = (uint16_t)sizeof(received);
        if (BZM_POLL_MAX_DRAIN_BYTES - total_bytes < capacity) {
            capacity = (uint16_t)(BZM_POLL_MAX_DRAIN_BYTES - total_bytes);
        }
        if (capacity == 0) break;
        length = BZM_SERIAL_rx(received, capacity, 0);
    }
    return emitted;
}

size_t bzm_serial_poll_with_reader(bzm_serial_transport_t * transport, uint16_t timeout_ms,
                                   bzm_serial_reader_t reader, void * reader_context)
{
    enum
    {
        BZM_POLL_BUFFER_SIZE = 128U,
        BZM_POLL_MAX_DRAIN_BYTES = BZM_SERIAL_RX_BUFFER_BYTES,
    };
    if (transport == NULL || !transport->io.initialized || timeout_ms == 0 || reader == NULL)
        return 0;

    uint8_t received[BZM_POLL_BUFFER_SIZE];
    /* uart_read_bytes may wait for the requested byte count, so asking for
     * the whole buffer turns every six-byte register response into a full
     * timeout. Block for exactly one byte, then drain everything already
     * buffered without waiting. The frame parser deliberately supports a
     * response split across multiple calls. */
    int16_t length = reader(reader_context, received, 1, timeout_ms);
    size_t total_bytes = 0;
    size_t emitted = 0;
    while (length > 0 && total_bytes < BZM_POLL_MAX_DRAIN_BYTES) {
        size_t accepted = (size_t) length;
        if (accepted > sizeof(received) || accepted > BZM_POLL_MAX_DRAIN_BYTES - total_bytes) {
            break;
        }
        emitted += bzm_serial_transport_ingest_bytes(transport, received, accepted,
                                                     (uint64_t) esp_timer_get_time());
        total_bytes += accepted;
        uint16_t capacity = (uint16_t) sizeof(received);
        if (BZM_POLL_MAX_DRAIN_BYTES - total_bytes < capacity) {
            capacity = (uint16_t) (BZM_POLL_MAX_DRAIN_BYTES - total_bytes);
        }
        if (capacity == 0)
            break;
        length = reader(reader_context, received, capacity, 0);
    }
    return emitted;
}

static uint16_t remaining_ms(int64_t deadline_us)
{
    int64_t remaining_us = deadline_us - esp_timer_get_time();
    if (remaining_us <= 0)
        return 0;
    uint64_t rounded_ms = ((uint64_t) remaining_us + 999U) / 1000U;
    return rounded_ms > UINT16_MAX ? UINT16_MAX : (uint16_t) rounded_ms;
}

static void clear_partial_frame(bzm_serial_transport_t * transport)
{
    pthread_mutex_lock(&transport->io.lock);
    transport->io.parser.buffered_length = 0;
    pthread_mutex_unlock(&transport->io.lock);
}

bzm_serial_probe_result_t bzm_serial_probe_noop(bzm_serial_transport_t * transport, uint8_t asic_id)
{
    if (transport == NULL || !transport->io.initialized)
        return BZM_SERIAL_PROBE_IO_ERROR;

    uint32_t discarded_before;
    uint32_t invalid_noop_before;
    pthread_mutex_lock(&transport->io.lock);
    if (transport->io.noop_pending) {
        pthread_mutex_unlock(&transport->io.lock);
        return BZM_SERIAL_PROBE_IO_ERROR;
    }
    transport->io.noop_pending = true;
    transport->io.noop_ready = false;
    transport->io.noop_asic_id = asic_id;
    discarded_before = transport->io.parser.discarded_bytes;
    invalid_noop_before = transport->io.invalid_noop_frames;
    pthread_mutex_unlock(&transport->io.lock);

    uint8_t request[4];
    size_t request_length = bzm_transport_encode_noop(asic_id, request, sizeof(request));
    if (request_length == 0 || BZM_SERIAL_send(request, (int)request_length, false) != (int)request_length) {
        goto IO_ERROR;
    }

    int64_t deadline = esp_timer_get_time() + 150000;
    while (true) {
        pthread_mutex_lock(&transport->io.lock);
        bool ready = transport->io.noop_ready;
        if (ready) {
            transport->io.noop_pending = false;
            transport->io.noop_ready = false;
        }
        pthread_mutex_unlock(&transport->io.lock);
        if (ready)
            return BZM_SERIAL_PROBE_RESPONSE;

        uint16_t wait_ms = remaining_ms(deadline);
        if (wait_ms == 0)
            break;
        (void) bzm_serial_poll(transport, wait_ms);
    }

    pthread_mutex_lock(&transport->io.lock);
    bool malformed_response =
        transport->io.parser.discarded_bytes != discarded_before || transport->io.invalid_noop_frames != invalid_noop_before;
    transport->io.noop_pending = false;
    transport->io.noop_ready = false;
    pthread_mutex_unlock(&transport->io.lock);
    clear_partial_frame(transport);
    return malformed_response ? BZM_SERIAL_PROBE_IO_ERROR : BZM_SERIAL_PROBE_NO_RESPONSE;

IO_ERROR:
    pthread_mutex_lock(&transport->io.lock);
    transport->io.noop_pending = false;
    transport->io.noop_ready = false;
    pthread_mutex_unlock(&transport->io.lock);
    BZM_SERIAL_clear_buffer();
    clear_partial_frame(transport);
    return BZM_SERIAL_PROBE_IO_ERROR;
}

static bool serial_read_register_op(void * context, uint8_t asic_id, uint16_t engine_id, uint8_t offset, void * data,
                                    size_t data_len)
{
    bzm_serial_transport_t * transport = context;
    if (transport == NULL || data == NULL || data_len == 0 || data_len > BZM_TDM_MAX_REGISTER_PAYLOAD_SIZE ||
        !bzm_serial_expect_register_reply(transport, asic_id, data_len)) {
        return false;
    }
    uint8_t request[12];
    size_t request_length = bzm_transport_encode_read(asic_id, engine_id, offset, data_len, request, sizeof(request));
    if (request_length == 0 || BZM_SERIAL_send(request, (int)request_length, false) != (int)request_length) {
        (void) bzm_serial_cancel_register_reply(transport, asic_id);
        return false;
    }

    /* Match the BIRDS TDM synchronous-register contract. A reply may take a
     * full second to surface while ordinary telemetry and results remain
     * active on the same stream. */
    int64_t deadline =
        esp_timer_get_time() + BZM_REGISTER_REPLY_TIMEOUT_US;
    bool received = false;
    while (!(received = bzm_serial_take_register_reply(transport, asic_id, data, data_len))) {
        uint16_t wait_ms = remaining_ms(deadline);
        if (wait_ms == 0)
            break;
        (void) bzm_serial_poll(transport, wait_ms);
    }
    if (received)
        return true;

    (void) bzm_serial_cancel_register_reply(transport, asic_id);
    BZM_SERIAL_clear_buffer();
    clear_partial_frame(transport);
    return false;
}

bool bzm_serial_read_register(bzm_serial_transport_t * transport, uint8_t asic_id, uint16_t engine_id, uint8_t offset, void * data,
                              size_t data_len)
{
    if (transport == NULL)
        return false;
    return serial_read_register_op(transport, asic_id, engine_id, offset, data, data_len);
}

static bool serial_register_writer(void * context, uint16_t engine_id, uint8_t offset, const void * data, size_t data_len)
{
    serial_register_context_t * writer = context;
    return writer != NULL && serial_write_register_raw(writer->asic_id, engine_id, offset, data, data_len);
}

bool bzm_serial_write_work(void * context, const bzm_work_t * work)
{
    bzm_serial_transport_t * transport = context;
    if (transport == NULL || work == NULL || transport->asic_count == 0) {
        return false;
    }

    for (size_t i = 0; i < transport->asic_count; ++i) {
        bzm_work_t chip_work = *work;
        if (!bzm_partition_nonce_range(work->starting_nonce, work->end_nonce, i, transport->asic_count, &chip_work.starting_nonce,
                                       &chip_work.end_nonce)) {
            return false;
        }
        if (!serial_write_register_raw(
                transport->asic_ids[i], work->engine_id,
                BZM_REG_START_NONCE, &chip_work.starting_nonce, 4) ||
            !serial_write_register_raw(
                transport->asic_ids[i], work->engine_id,
                BZM_REG_END_NONCE, &chip_work.end_nonce, 4)) {
            return false;
        }
    }
    /* BIRDS preloads the per-ASIC nonce quarters, then multicasts the common
     * engine job. Keep the explicit range writes above, but send the large
     * midstate payload only once to all four addressed ASICs. */
    serial_register_context_t writer = {
        .asic_id = BZM_ALL_ASICS,
    };
    return bzm_transport_program_broadcast_work(
        work, serial_register_writer, &writer);
}

bool bzm_serial_flush(void * context)
{
    bzm_serial_transport_t * transport = context;
    if (transport == NULL)
        return false;

    serial_register_context_t writer = {
        .asic_id = BZM_ALL_ASICS,
    };
    bool success = transport->asic_count != 0 &&
        bzm_transport_program_flush(
            transport->engine_count,
            serial_register_writer, &writer);
    BZM_SERIAL_clear_buffer();
    if (transport->io.initialized)
        reset_io_state(transport, false, false);
    return success;
}

bool bzm_serial_read_result(bzm_serial_transport_t * transport, bzm_raw_result_t * result, uint16_t timeout_ms)
{
    if (transport == NULL || !transport->io.initialized || result == NULL)
        return false;
    if (dequeue_result(transport, result))
        return true;

    int64_t deadline = esp_timer_get_time() + (int64_t) timeout_ms * 1000;
    while (true) {
        uint16_t wait_ms = remaining_ms(deadline);
        if (wait_ms == 0)
            return false;
        (void) bzm_serial_poll(transport, wait_ms);
        if (dequeue_result(transport, result))
            return true;
    }
}
