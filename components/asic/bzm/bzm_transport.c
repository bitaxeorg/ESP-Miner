#include "bzm/telemetry_decode.h"
#include "bzm/transport.h"
#include "bzm/work_registers.h"

#include <string.h>


enum { BZM_REGISTER_REPLY_TIMEOUT_US = 1000000 };

typedef struct
{
    bzm_serial_transport_t *transport;
    uint8_t asic_id;
    bool multicast;
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

static void transport_lock(bzm_serial_transport_t *transport)
{
    if (transport->ops.lock != NULL) transport->ops.lock(transport->context);
}

static void transport_unlock(bzm_serial_transport_t *transport)
{
    if (transport->ops.unlock != NULL) transport->ops.unlock(transport->context);
}

bool bzm_serial_transport_init(bzm_serial_transport_t *transport,
                               const bzm_serial_ops_t *ops, void *context)
{
    if (transport == NULL || ops == NULL || ops->read == NULL ||
        ops->write == NULL || ops->clear_rx == NULL || ops->now_us == NULL ||
        (ops->lock == NULL) != (ops->unlock == NULL)) return false;
    if (transport->io.initialized) return false;
    transport->ops = *ops;
    transport->context = context;
    memset(&transport->io, 0, sizeof(transport->io));
    transport->io.initialized = true;
    bzm_frame_parser_init(&transport->io.parser, transport_frame_handler, transport);
    bzm_telemetry_store_init(&transport->io.telemetry);
    return true;
}

void bzm_serial_transport_deinit(bzm_serial_transport_t *transport)
{
    if (transport == NULL) return;
    memset(&transport->io, 0, sizeof(transport->io));
    memset(&transport->ops, 0, sizeof(transport->ops));
    transport->context = NULL;
}

static void reset_io_state(bzm_serial_transport_t * transport, bool reset_telemetry, bool reset_counters)
{
    bzm_serial_io_state_t * io = &transport->io;
    transport_lock(transport);
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
    transport_unlock(transport);
}

size_t bzm_serial_transport_ingest_bytes(bzm_serial_transport_t * transport, const uint8_t * data, size_t data_length,
                                         uint64_t timestamp_us)
{
    if (transport == NULL || !transport->io.initialized || (data == NULL && data_length != 0)) {
        return 0;
    }
    transport_lock(transport);
    size_t emitted = bzm_frame_parser_feed(&transport->io.parser, data, data_length, timestamp_us);
    transport_unlock(transport);
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

static size_t encode_write(uint8_t opcode, uint8_t asic_id, uint16_t engine_id, uint8_t offset,
                           const uint8_t *data, size_t data_len, uint8_t *encoded, size_t encoded_capacity)
{
    if (data == NULL || data_len == 0 || data_len > 256 || encoded == NULL || engine_id >= BZM_MAX_ENGINE_COUNT) {
        return 0;
    }

    size_t cursor = 0;
    if (!append_word(0x100 | asic_id, encoded, encoded_capacity, &cursor) ||
        !append_word(opcode | ((engine_id >> 8) & 0x0f), encoded, encoded_capacity, &cursor) ||
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

size_t bzm_transport_encode_write(uint8_t asic_id, uint16_t engine_id, uint8_t offset, const uint8_t *data,
                                  size_t data_len, uint8_t *encoded, size_t encoded_capacity)
{
    return encode_write(0x20, asic_id, engine_id, offset, data, data_len, encoded, encoded_capacity);
}

size_t bzm_transport_encode_multicast(uint8_t asic_id, uint8_t group_id, uint8_t offset, const uint8_t *data,
                                      size_t data_len, uint8_t *encoded, size_t encoded_capacity)
{
    if (group_id >= BZM_ENGINE_ROWS && group_id != 0xff) return 0;
    return encode_write(0x40, asic_id, group_id, offset, data, data_len, encoded, encoded_capacity);
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

bool bzm_partition_row_nonce_range(uint8_t asic_id, uint16_t engine_id,
                                   uint32_t *partition_start, uint32_t *partition_end)
{
    bzm_engine_location_t engine;
    size_t chip, columns = 0, rank = 0;
    if (!bzm_topology_asic_index(asic_id, &chip) ||
        !bzm_topology_from_physical_id(engine_id, &engine)) return false;
    for (uint8_t column = 0; column < BZM_ENGINE_COLUMNS; ++column) {
        if (!bzm_topology_coordinate_is_valid(engine.row, column)) continue;
        if (column < engine.column) ++rank;
        ++columns;
    }
    return bzm_partition_nonce_range(0, UINT32_MAX, chip * columns + rank,
        BZM_MAX_ASIC_COUNT * columns, partition_start, partition_end);
}

static bool serial_write_register_raw(bzm_serial_transport_t *transport, uint8_t asic_id, uint16_t engine_id, uint8_t offset, const void * data, size_t data_len)
{
    uint8_t encoded[(32 + 6) * 2];
    if (data_len > 32)
        return false;
    size_t length = bzm_transport_encode_write(asic_id, engine_id, offset, data, data_len, encoded, sizeof(encoded));
    return length != 0 && transport->ops.write(transport->context, encoded, length) == (int)length;
}

bool bzm_serial_write_register_to(bzm_serial_transport_t * transport, uint8_t asic_id, uint16_t engine_id, uint8_t offset,
                                  const void * data, size_t data_len)
{
    if (transport == NULL || !transport->io.initialized)
        return false;
    return serial_write_register_raw(transport, asic_id, engine_id, offset, data, data_len);
}

static bool dequeue_result(bzm_serial_transport_t * transport, bzm_raw_result_t * result)
{
    if (transport == NULL || !transport->io.initialized || result == NULL)
        return false;

    bzm_serial_io_state_t * io = &transport->io;
    transport_lock(transport);
    bool available = io->pending_result_length != 0;
    if (available) {
        *result = io->pending_results[io->pending_result_head];
        io->pending_result_head = (io->pending_result_head + 1) % BZM_PENDING_RESULT_COUNT;
        io->pending_result_length--;
    }
    transport_unlock(transport);
    return available;
}

void bzm_serial_discard_pending_results(bzm_serial_transport_t *transport)
{
    if (transport == NULL || !transport->io.initialized)
        return;
    transport_lock(transport);
    transport->io.pending_result_head = 0;
    transport->io.pending_result_length = 0;
    transport_unlock(transport);
}

bool bzm_serial_expect_register_reply(bzm_serial_transport_t * transport, uint8_t asic_id, size_t payload_length)
{
    if (transport == NULL || !transport->io.initialized || !addressed_asic(asic_id) || payload_length == 0 ||
        payload_length > BZM_TDM_MAX_REGISTER_PAYLOAD_SIZE) {
        return false;
    }

    bzm_serial_io_state_t * io = &transport->io;
    transport_lock(transport);
    bool accepted = !io->register_pending && bzm_frame_parser_expect_register(&io->parser, asic_id, payload_length);
    if (accepted) {
        io->register_pending = true;
        io->register_ready = false;
        io->register_asic_id = asic_id;
        io->register_length = payload_length;
    }
    transport_unlock(transport);
    return accepted;
}

bool bzm_serial_take_register_reply(bzm_serial_transport_t * transport, uint8_t asic_id, void * data, size_t data_length)
{
    if (transport == NULL || !transport->io.initialized || data == NULL)
        return false;

    bzm_serial_io_state_t * io = &transport->io;
    transport_lock(transport);
    bool ready =
        io->register_pending && io->register_ready && io->register_asic_id == asic_id && io->register_length == data_length;
    if (ready) {
        memcpy(data, io->register_data, data_length);
        io->register_pending = false;
        io->register_ready = false;
        io->register_length = 0;
    }
    transport_unlock(transport);
    return ready;
}

bool bzm_serial_cancel_register_reply(bzm_serial_transport_t * transport, uint8_t asic_id)
{
    if (transport == NULL || !transport->io.initialized)
        return false;

    bzm_serial_io_state_t * io = &transport->io;
    transport_lock(transport);
    bool pending = io->register_pending && io->register_asic_id == asic_id;
    if (pending) {
        (void) bzm_frame_parser_cancel_register(&io->parser, asic_id);
        io->register_pending = false;
        io->register_ready = false;
        io->register_length = 0;
    }
    transport_unlock(transport);
    return pending;
}

bool bzm_serial_get_telemetry(bzm_serial_transport_t * transport, uint8_t asic_id, bzm_telemetry_sample_t * sample)
{
    if (transport == NULL || !transport->io.initialized || sample == NULL || !addressed_asic(asic_id)) {
        return false;
    }

    transport_lock(transport);
    const bzm_telemetry_sample_t * stored = bzm_telemetry_store_get(&transport->io.telemetry, asic_id);
    bool received = stored != NULL && stored->received;
    if (stored != NULL)
        *sample = *stored;
    transport_unlock(transport);
    return received;
}

bool bzm_serial_get_telemetry_snapshot(bzm_serial_transport_t * transport, bzm_telemetry_store_t * snapshot)
{
    if (transport == NULL || !transport->io.initialized || snapshot == NULL)
        return false;
    transport_lock(transport);
    *snapshot = transport->io.telemetry;
    transport_unlock(transport);
    return true;
}

bool bzm_serial_get_parser_stats(bzm_serial_transport_t * transport, bzm_serial_parser_stats_t * stats)
{
    if (transport == NULL || !transport->io.initialized || stats == NULL)
        return false;

    bzm_serial_io_state_t * io = &transport->io;
    transport_lock(transport);
    *stats = (bzm_serial_parser_stats_t){
        .emitted_frames = io->parser.emitted_frames,
        .discarded_bytes = io->parser.discarded_bytes,
        .invalid_noop_frames = io->invalid_noop_frames,
        .buffered_bytes = io->parser.buffered_length,
    };
    transport_unlock(transport);
    return true;
}

bzm_serial_poll_result_t bzm_serial_poll(bzm_serial_transport_t *transport, uint16_t timeout_ms)
{
    enum
    {
        BZM_POLL_BUFFER_SIZE = 128U,
        BZM_POLL_MAX_DRAIN_BYTES = BZM_SERIAL_POLL_MAX_BYTES,
    };
    bzm_serial_poll_result_t result = {.status = BZM_OK};
    if (transport == NULL)
        return (bzm_serial_poll_result_t){.status = BZM_ERR_INVALID_ARG};
    if (!transport->io.initialized)
        return (bzm_serial_poll_result_t){.status = BZM_ERR_INVALID_STATE};
    uint8_t received[BZM_POLL_BUFFER_SIZE];
    /* uart_read_bytes may wait for the requested byte count, so asking for
     * the whole buffer turns every six-byte register response into a full
     * timeout. Block for exactly one byte, then drain everything already
     * buffered without waiting. The frame parser deliberately supports a
     * response split across multiple calls. A zero timeout only drains
     * buffered bytes, including errors, without waiting for new input. */
    uint16_t capacity = 1;
    while (result.bytes_received < BZM_POLL_MAX_DRAIN_BYTES) {
        int16_t length = transport->ops.read(transport->context, received, capacity, timeout_ms);
        if (length < 0 || length > capacity) {
            result.status = BZM_ERR_IO;
            break;
        }
        if (length == 0)
            break;
        result.bytes_received += (size_t)length;
        result.frames_emitted += bzm_serial_transport_ingest_bytes(
            transport, received, (size_t)length, transport->ops.now_us(transport->context));
        capacity = (uint16_t)sizeof(received);
        if (BZM_POLL_MAX_DRAIN_BYTES - result.bytes_received < capacity) {
            capacity = (uint16_t)(BZM_POLL_MAX_DRAIN_BYTES - result.bytes_received);
        }
        timeout_ms = 0;
    }
    if (result.status == BZM_ERR_IO && transport->ops.on_io_error != NULL)
        transport->ops.on_io_error(transport->context);
    return result;
}

static uint16_t remaining_ms(bzm_serial_transport_t *transport, uint64_t deadline_us)
{
    uint64_t now = transport->ops.now_us(transport->context);
    if (deadline_us <= now) return 0;
    uint64_t rounded_ms = (deadline_us - now + 999U) / 1000U;
    return rounded_ms > UINT16_MAX ? UINT16_MAX : (uint16_t) rounded_ms;
}

static void clear_partial_frame(bzm_serial_transport_t * transport)
{
    transport_lock(transport);
    transport->io.parser.buffered_length = 0;
    transport_unlock(transport);
}

bzm_serial_probe_result_t bzm_serial_probe_noop(bzm_serial_transport_t * transport, uint8_t asic_id)
{
    if (transport == NULL || !transport->io.initialized)
        return BZM_SERIAL_PROBE_IO_ERROR;

    uint32_t discarded_before;
    uint32_t invalid_noop_before;
    transport_lock(transport);
    if (transport->io.noop_pending) {
        transport_unlock(transport);
        return BZM_SERIAL_PROBE_IO_ERROR;
    }
    transport->io.noop_pending = true;
    transport->io.noop_ready = false;
    transport->io.noop_asic_id = asic_id;
    discarded_before = transport->io.parser.discarded_bytes;
    invalid_noop_before = transport->io.invalid_noop_frames;
    transport_unlock(transport);

    uint8_t request[4];
    size_t request_length = bzm_transport_encode_noop(asic_id, request, sizeof(request));
    if (request_length == 0 || transport->ops.write(transport->context, request, request_length) != (int)request_length) {
        goto IO_ERROR;
    }

    uint64_t deadline = transport->ops.now_us(transport->context) + 150000;
    while (true) {
        transport_lock(transport);
        bool ready = transport->io.noop_ready;
        if (ready) {
            transport->io.noop_pending = false;
            transport->io.noop_ready = false;
        }
        transport_unlock(transport);
        if (ready)
            return BZM_SERIAL_PROBE_RESPONSE;

        uint16_t wait_ms = remaining_ms(transport, deadline);
        if (wait_ms == 0)
            break;
        if (bzm_serial_poll(transport, wait_ms).status != BZM_OK) goto IO_ERROR;
    }

    transport_lock(transport);
    bool malformed_response =
        transport->io.parser.discarded_bytes != discarded_before || transport->io.invalid_noop_frames != invalid_noop_before;
    transport->io.noop_pending = false;
    transport->io.noop_ready = false;
    transport_unlock(transport);
    clear_partial_frame(transport);
    return malformed_response ? BZM_SERIAL_PROBE_IO_ERROR : BZM_SERIAL_PROBE_NO_RESPONSE;

IO_ERROR:
    transport_lock(transport);
    transport->io.noop_pending = false;
    transport->io.noop_ready = false;
    transport_unlock(transport);
    transport->ops.clear_rx(transport->context);
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
    if (request_length == 0 || transport->ops.write(transport->context, request, request_length) != (int)request_length) {
        (void) bzm_serial_cancel_register_reply(transport, asic_id);
        return false;
    }

    /* Match the BIRDS TDM synchronous-register contract. A reply may take a
     * full second to surface while ordinary telemetry and results remain
     * active on the same stream. */
    uint64_t deadline =
        transport->ops.now_us(transport->context) + BZM_REGISTER_REPLY_TIMEOUT_US;
    bool received = false;
    while (!(received = bzm_serial_take_register_reply(transport, asic_id, data, data_len))) {
        uint16_t wait_ms = remaining_ms(transport, deadline);
        if (wait_ms == 0)
            break;
        if (bzm_serial_poll(transport, wait_ms).status != BZM_OK) break;
    }
    if (received)
        return true;

    (void) bzm_serial_cancel_register_reply(transport, asic_id);
    transport->ops.clear_rx(transport->context);
    clear_partial_frame(transport);
    return false;
}

bool bzm_serial_read_register(bzm_serial_transport_t * transport, uint8_t asic_id, uint16_t engine_id, uint8_t offset, void * data,
                              size_t data_len)
{
    if (transport == NULL || !transport->io.initialized)
        return false;
    return serial_read_register_op(transport, asic_id, engine_id, offset, data, data_len);
}

static bool serial_register_writer(void * context, uint16_t engine_id, uint8_t offset, const void * data, size_t data_len)
{
    serial_register_context_t * writer = context;
    if (writer != NULL && writer->multicast) {
        uint8_t encoded[(32 + 6) * 2];
        if (engine_id >= BZM_ENGINE_ROWS) return false;
        size_t length = bzm_transport_encode_multicast(writer->asic_id, engine_id,
            offset, data, data_len, encoded, sizeof(encoded));
        return length != 0 && writer->transport->ops.write(
            writer->transport->context, encoded, length) == (int)length;
    }
    return writer != NULL && serial_write_register_raw(writer->transport, writer->asic_id, engine_id, offset, data, data_len);
}

bool bzm_serial_write_work(void * context, const bzm_work_t * work)
{
    bzm_serial_transport_t * transport = context;
    if (transport == NULL || !transport->io.initialized || work == NULL ||
        transport->asic_count != BZM_MAX_ASIC_COUNT ||
        work->starting_nonce != 0 || work->end_nonce != UINT32_MAX) {
        return false;
    }

    /* Nonce partitions persist across replacement jobs and live PLL changes. */
    serial_register_context_t writer = {
        .transport = transport,
        .asic_id = BZM_ALL_ASICS,
        .multicast = work->multicast_row,
    };
    return bzm_transport_program_broadcast_work(
        work, serial_register_writer, &writer);
}

bool bzm_serial_flush(void * context)
{
    bzm_serial_transport_t * transport = context;
    if (transport == NULL || !transport->io.initialized)
        return false;

    serial_register_context_t writer = {
        .transport = transport,
        .asic_id = BZM_ALL_ASICS,
    };
    bool success = transport->asic_count != 0 &&
        bzm_transport_program_flush(
            transport->engine_count,
            serial_register_writer, &writer);
    transport->ops.clear_rx(transport->context);
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

    uint64_t deadline = transport->ops.now_us(transport->context) + (uint64_t) timeout_ms * 1000;
    while (true) {
        uint16_t wait_ms = remaining_ms(transport, deadline);
        if (wait_ms == 0)
            return false;
        if (bzm_serial_poll(transport, wait_ms).status != BZM_OK) return false;
        if (dequeue_result(transport, result))
            return true;
    }
}
