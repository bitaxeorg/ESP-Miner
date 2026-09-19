#ifndef BZM_TRANSPORT_H
#define BZM_TRANSPORT_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bzm.h"
#include "bzm_frame_parser.h"
#include "bzm_reactor.h"
#include "bzm_telemetry.h"

#define BZM_PENDING_RESULT_COUNT 256U
#define BZM_RESULT_DESIGN_RATE_PER_SECOND 200U
#define BZM_RESULT_MAX_DISPATCH_BLACKOUT_MS 1250U
#define BZM_ENGINE_REG_STATUS 0x00U
#define BZM_ENGINE_REG_CONFIG 0x01U

typedef struct
{
    uint32_t emitted_frames;
    uint32_t discarded_bytes;
    uint32_t invalid_noop_frames;
    size_t buffered_bytes;
} bzm_serial_parser_stats_t;

typedef struct
{
    pthread_mutex_t lock;
    bool initialized;
    bzm_frame_parser_t parser;
    bzm_telemetry_store_t telemetry;

    bzm_raw_result_t pending_results[BZM_PENDING_RESULT_COUNT];
    size_t pending_result_head;
    size_t pending_result_length;

    bool register_pending;
    bool register_ready;
    uint8_t register_asic_id;
    uint8_t register_length;
    uint8_t register_data[BZM_TDM_MAX_REGISTER_PAYLOAD_SIZE];

    bool noop_pending;
    bool noop_ready;
    uint8_t noop_asic_id;
    uint32_t invalid_noop_frames;
} bzm_serial_io_state_t;

typedef struct
{
    uint8_t asic_ids[BZM_MAX_ASIC_COUNT];
    uint8_t asic_count;
    uint16_t engine_count;
    bzm_serial_io_state_t io;
} bzm_serial_transport_t;

static inline bool bzm_result_queue_capacity_covers(uint32_t result_capacity, uint32_t results_per_second, uint32_t blackout_ms)
{
    return (uint64_t) result_capacity * 1000U >= (uint64_t) results_per_second * blackout_ms;
}

typedef enum
{
    BZM_SERIAL_PROBE_RESPONSE = 0,
    BZM_SERIAL_PROBE_NO_RESPONSE,
    BZM_SERIAL_PROBE_IO_ERROR,
} bzm_serial_probe_result_t;

typedef bool (*bzm_register_writer_t)(void * context, uint16_t engine_id, uint8_t offset, const void * data, size_t data_len);
typedef int16_t (*bzm_serial_reader_t)(void * context, uint8_t * data, uint16_t size, uint16_t timeout_ms);

size_t bzm_transport_encode_write(uint8_t asic_id, uint16_t engine_id, uint8_t offset, const uint8_t * data, size_t data_len,
                                  uint8_t * encoded, size_t encoded_capacity);
size_t bzm_transport_encode_read(uint8_t asic_id, uint16_t engine_id, uint8_t offset, size_t data_len, uint8_t * encoded,
                                 size_t encoded_capacity);
size_t bzm_transport_encode_noop(uint8_t asic_id, uint8_t * encoded, size_t encoded_capacity);

bool bzm_partition_nonce_range(uint32_t starting_nonce, uint32_t end_nonce, size_t partition, size_t partition_count,
                               uint32_t * partition_start, uint32_t * partition_end);

/* A transport must not be copied after this initialization. */
bool bzm_serial_transport_init(bzm_serial_transport_t * transport);
void bzm_serial_transport_deinit(bzm_serial_transport_t * transport);
size_t bzm_serial_transport_ingest_bytes(bzm_serial_transport_t * transport, const uint8_t * data, size_t data_length,
                                         uint64_t timestamp_us);
size_t bzm_serial_poll(bzm_serial_transport_t * transport, uint16_t timeout_ms);
/* Testable core of bzm_serial_poll. After the first bounded wait it drains
 * every byte already queued by the UART driver, up to a guard larger than the
 * installed RX ring, so one safety-monitor tick cannot leave a stale backlog. */
size_t bzm_serial_poll_with_reader(bzm_serial_transport_t * transport, uint16_t timeout_ms, bzm_serial_reader_t reader,
                                   void * reader_context);
bzm_serial_probe_result_t bzm_serial_probe_noop(bzm_serial_transport_t * transport, uint8_t asic_id);

bool bzm_serial_expect_register_reply(bzm_serial_transport_t * transport, uint8_t asic_id, size_t payload_length);
bool bzm_serial_take_register_reply(bzm_serial_transport_t * transport, uint8_t asic_id, void * data, size_t data_length);
bool bzm_serial_cancel_register_reply(bzm_serial_transport_t * transport, uint8_t asic_id);

bool bzm_serial_get_telemetry(bzm_serial_transport_t * transport, uint8_t asic_id, bzm_telemetry_sample_t * sample);
bool bzm_serial_get_telemetry_snapshot(bzm_serial_transport_t * transport, bzm_telemetry_store_t * snapshot);
bool bzm_serial_get_parser_stats(bzm_serial_transport_t * transport, bzm_serial_parser_stats_t * stats);
void bzm_serial_discard_pending_results(bzm_serial_transport_t *transport);

bool bzm_serial_write_register_to(bzm_serial_transport_t * transport, uint8_t asic_id, uint16_t engine_id, uint8_t offset,
                                  const void * data, size_t data_len);
bool bzm_serial_read_register(bzm_serial_transport_t * transport, uint8_t asic_id, uint16_t engine_id, uint8_t offset, void * data,
                              size_t data_len);
bool bzm_transport_program_work(const bzm_work_t * work, bzm_register_writer_t writer, void * writer_context);
/* Program common job fields for an all-ASIC multicast after each ASIC's
 * persistent nonce quarter has been written separately. */
bool bzm_transport_program_broadcast_work(
    const bzm_work_t *work, bzm_register_writer_t writer,
    void *writer_context);
/* Queue a deterministic, 64-leading-zero sentinel as both current and
 * pending work. It exercises the hashing datapath without opening ordinary
 * mining dispatch. */
bool bzm_transport_program_startup_work(uint16_t engine_id, bzm_register_writer_t writer, void * writer_context);
bool bzm_transport_program_flush(uint16_t engine_count, bzm_register_writer_t writer, void * writer_context);
bool bzm_serial_write_work(void * context, const bzm_work_t * work);
bool bzm_serial_flush(void * context);
bool bzm_serial_read_result(bzm_serial_transport_t * transport, bzm_raw_result_t * result, uint16_t timeout_ms);

#endif // BZM_TRANSPORT_H
