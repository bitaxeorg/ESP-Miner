#include "check.h"
#include "bzm/posix.h"
#include "bzm/reactor.h"
#include "bzm/transport.h"
#include "bzm/work_registers.h"
#include <string.h>

static unsigned writes, commits, flushes;
static size_t bytes;
static uint8_t expected_row;

static int write_bytes(void *context, const uint8_t *data, size_t size)
{
    (void)context;
    CHECK(size >= 14 && data[0] == 0xff && data[1] == 1);
    CHECK(data[2] == 0x40 && data[4] == expected_row);
    CHECK(data[6] != BZM_REG_START_NONCE && data[6] != BZM_REG_END_NONCE);
    if (data[6] == BZM_REG_TIMESTAMP_COUNT) CHECK(data[10] == 0xff);
    if (data[6] == BZM_REG_JOB_CONTROL) { CHECK(data[10] == 3); ++commits; }
    bytes += size;
    ++writes;
    return (int)size;
}
static int16_t read_bytes(void *context, uint8_t *data, uint16_t size, uint16_t timeout)
{ (void)context; (void)data; (void)size; (void)timeout; return 0; }
static void clear_rx(void *context) { (void)context; }
static uint64_t now_us(void *context) { (void)context; return 0; }
static bool flush(void *context) { (void)context; ++flushes; return true; }

static void check_nonce_partitions(void)
{
    unsigned engines = 0;
    for (uint8_t row = 0; row < 20; ++row) {
        uint64_t next = 0;
        unsigned row_engines = 0;
        for (size_t chip = 0; chip < 4; ++chip) {
            for (uint8_t column = 0; column < 12; ++column) {
                uint32_t start, end;
                bool hole = (row == 0 && (column == 4 || column == 5)) ||
                            (row == 19 && (column == 5 || column == 11));
                bool valid = bzm_partition_row_nonce_range(bzm_asic_wire_ids[chip],
                    (column << 6) | row, &start, &end);
                CHECK(valid == !hole);
                if (hole) continue;
                CHECK(start == next && end > start);
                next = (uint64_t)end + 1;
                ++row_engines;
            }
        }
        CHECK(row_engines == (row == 0 || row == 19 ? 40U : 48U));
        CHECK(next == (UINT64_C(1) << 32));
        engines += row_engines;
    }
    CHECK(engines == 944);
    uint32_t start, end;
    CHECK(!bzm_partition_row_nonce_range(11, 0, &start, &end));
    CHECK(!bzm_partition_row_nonce_range(10, 20, &start, &end));
    CHECK(!bzm_partition_row_nonce_range(10, 12 << 6, &start, &end));
    CHECK(!bzm_partition_row_nonce_range(10, 0, NULL, &end));
}

static void assign(bzm_reactor_t *reactor, asic_job_t *job)
{
    expected_row = reactor->next_engine / 2 + (reactor->next_engine % 2) * 10;
    unsigned before = writes;
    bzm_work_t work;
    CHECK(bzm_reactor_assign(reactor, job, &work) == BZM_ASSIGN_OK);
    CHECK(work.multicast_row && work.engine_id == expected_row);
    CHECK(writes == before + 14);
}

int main(void)
{
    bzm_platform_ops_t platform;
    bzm_platform_posix_defaults(&platform);
    CHECK(bzm_platform_bind(&platform, NULL));
    check_nonce_partitions();
    static bzm_reactor_t reactor;
    bzm_job_store_t store;
    CHECK(bzm_job_store_init(&store));
    bzm_serial_transport_t serial = {.asic_count = 4};
    const bzm_serial_ops_t serial_ops = {.write = write_bytes, .read = read_bytes,
        .clear_rx = clear_rx, .now_us = now_us};
    CHECK(bzm_serial_transport_init(&serial, &serial_ops, NULL));
    const bzm_transport_ops_t ops = {.write_work = bzm_serial_write_work, .flush = flush};
    bzm_reactor_config_t config = {.engine_count = 20, .multicast_rows = true,
        .timestamp_count = 127, .lead_zeros = 36, .nonce_offset = BZM_NONCE_GAP};
    CHECK(bzm_reactor_init(&reactor, &store, &config, &ops, &serial));
    asic_job_t job = {.version = 0x20000000, .version_mask = 0x6000,
        .ntime = 1000, .nbits = 0x1702369d};
    for (size_t i = 0; i < 20; ++i) { job.ntime = 1000 + i; assign(&reactor, &job); }
    CHECK(commits == 20 && bytes == 9240);
    bzm_result_t result;
    bzm_raw_result_t raw = {.status = 8, .time = 127, .nonce = BZM_NONCE_GAP + 123};
    for (size_t chip = 0; chip < 4; ++chip) {
        raw.asic_id = bzm_asic_wire_ids[chip];
        for (uint16_t engine = 0; engine < 236; ++engine) {
            bzm_engine_location_t location;
            CHECK(bzm_topology_activation_at(engine, BZM_ENGINE_STACK_BOTTOM, &location));
            raw.engine_id = location.physical_id;
            for (uint8_t variant = 0; variant < 4; ++variant) {
                raw.sequence_id = variant;
                CHECK(bzm_reactor_map_result(&reactor, &raw, &result));
                CHECK(result.job.ntime == 1000U + 2U * (location.row % 10) + location.row / 10);
                CHECK(result.engine_id == location.topology_index && result.asic_index == chip);
                CHECK(result.micro_job_id == variant && result.nonce == __builtin_bswap32(123U));
                CHECK(result.final_version == 0x20000000U + variant * 0x2000U);
            }
        }
    }
    // The returned countdown covers all 128 timestamps without including the
    // continuous-clock bit from the programmed register in result arithmetic.
    for (unsigned countdown = 0; countdown < 128; ++countdown) {
        raw.time = (uint8_t)countdown;
        CHECK(bzm_reactor_map_result(&reactor, &raw, &result));
        CHECK(result.final_ntime == result.job.ntime + 127 - countdown);
    }
    raw.time = 128;
    CHECK(!bzm_reactor_map_result(&reactor, &raw, &result));
    raw.time = 127;
    // A retired row generation remains attributable until a clean job invalidates it.
    raw.asic_id = 10; raw.engine_id = 7 << 6; raw.sequence_id = 0;
    assign(&reactor, &job);
    CHECK(bzm_reactor_map_result(&reactor, &raw, &result) && result.job.ntime == 1000);
    CHECK(bzm_reactor_invalidate_work(&reactor));
    CHECK(!bzm_reactor_map_result(&reactor, &raw, &result));
    assign(&reactor, &job); // Row 10; no result from row 0 can acquire its job.
    raw.sequence_id = 4;
    CHECK(!bzm_reactor_map_result(&reactor, &raw, &result));
    raw.engine_id |= 10;
    CHECK(bzm_reactor_map_result(&reactor, &raw, &result));
    raw.engine_id = (5 << 6) | 19;
    CHECK(!bzm_reactor_map_result(&reactor, &raw, &result));

    // Every group preserves the reserved-ID barrier, including after invalidation.
    while (reactor.next_engine_sequence[reactor.next_engine] < 63) assign(&reactor, &job);
    CHECK(bzm_reactor_assign(&reactor, &job, NULL) == BZM_ASSIGN_FLUSH_REQUIRED);
    CHECK(bzm_reactor_begin_flush(&reactor));
    bzm_reactor_finish_flush(&reactor);
    CHECK(flushes == 1 && bzm_reactor_results_quarantined(&reactor));
    for (size_t i = 0; i < 20; ++i) {
        CHECK(bzm_reactor_results_quarantined(&reactor));
        assign(&reactor, &job);
    }
    CHECK(!bzm_reactor_results_quarantined(&reactor));
    config.engine_count = 19;
    CHECK(!bzm_reactor_init(&reactor, &store, &config, &ops, &serial));
    bzm_serial_transport_deinit(&serial);
    bzm_job_store_destroy(&store);
    puts("grouped jobs: all 944 nonce partitions, multicast frames, result ownership and sequence reuse passed");
    return 0;
}
