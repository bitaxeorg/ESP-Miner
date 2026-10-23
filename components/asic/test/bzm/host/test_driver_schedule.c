#include "check.h"
#include "bzm/posix.h"
#include "bzm/serial_buffers.h"

#define bzm_port_serial_prepare_session fixture_serial_prepare
#define bzm_control_set_asic_reset fixture_reset
#define bzm_control_pulse_asic_reset fixture_pulse
#define bzm_bringup_start fixture_chip_start
#define bzm_bringup_live_frequency_domains_step fixture_clock_step
#include "../../../bzm/bzm_driver.c"

static struct {
    int64_t now, tx_done;
    size_t bytes;
    unsigned sleeps, polls, requests, writes, drains, clock_steps, nonce_writes;
    bool dispatching, allowed, fail_drain, fail_nonce, retire_during_hash, hashing_nonce;
} fixture;

static int64_t now_us(void *context) { (void)context; return fixture.now; }
static void sleep_us(void *context, uint32_t us)
{ (void)context; fixture.now += us; ++fixture.sleeps; }
static bool authorize(void *context) { (void)context; return fixture.allowed; }
static void request_work(void *context)
{
    CHECK(context == &fixture);
    ++fixture.requests;
}
static bzm_status_t hash_bytes(void *context, const uint8_t *data, size_t size, uint8_t digest[32])
{
    (void)context; (void)data;
    if (size != 80 && !fixture.hashing_nonce) return BZM_ERR_NOT_SUPPORTED;
    fixture.hashing_nonce = size == 80;
    if (fixture.retire_during_hash) {
        fixture.retire_during_hash = false;
        CHECK(bzm_driver_clear_work(NULL));
    }
    memset(digest, 0, 32);
    return BZM_OK;
}
static int16_t read_bytes(void *context, uint8_t *data, uint16_t size, uint16_t timeout_ms)
{
    (void)context; (void)data; (void)size;
    if (fixture.dispatching) CHECK(timeout_ms == 0);
    fixture.now += (int64_t)timeout_ms * 1000;
    ++fixture.polls;
    return 0;
}
static int write_bytes(void *context, const uint8_t *data, size_t size)
{
    (void)context;
    if (data[6] == BZM_REG_START_NONCE || data[6] == BZM_REG_END_NONCE) {
        CHECK(!fixture.dispatching && DRIVER_BRINGUP.running);
        if (fixture.fail_nonce) return -1;
        CHECK(data[2] >> 4 == 2);
        uint16_t engine = ((data[2] & 15) << 8) | data[4];
        uint32_t start, end;
        CHECK(bzm_partition_row_nonce_range(data[0], engine, &start, &end));
        uint32_t value = (uint32_t)data[10] | ((uint32_t)data[12] << 8) |
            ((uint32_t)data[14] << 16) | ((uint32_t)data[16] << 24);
        CHECK(value == (data[6] == BZM_REG_START_NONCE ? start : end));
        ++fixture.nonce_writes;
    } else if (fixture.dispatching) {
        CHECK(data[0] == BZM_ALL_ASICS && data[2] == 0x40 && data[4] < BZM_ENGINE_ROWS);
        if (data[6] == BZM_REG_TIMESTAMP_COUNT) CHECK(data[10] == 0xff);
    }
    if (fixture.tx_done < fixture.now) fixture.tx_done = fixture.now;
    fixture.tx_done += (int64_t)size * 5; // 2 Mbaud 8N1 with a bounded TX ring.
    int64_t room_at = fixture.tx_done - BZM_SERIAL_TX_BUFFER_BYTES * 5;
    if (fixture.now < room_at) fixture.now = room_at;
    fixture.bytes += size;
    ++fixture.writes;
    return (int)size;
}
static bzm_status_t wait_tx(void *context, uint32_t timeout_ms)
{
    (void)context;
    ++fixture.drains;
    if (fixture.fail_drain) return BZM_ERR_IO;
    CHECK(fixture.tx_done - fixture.now <= (int64_t)timeout_ms * 1000);
    if (fixture.now < fixture.tx_done) fixture.now = fixture.tx_done;
    return BZM_OK;
}
static bzm_status_t clear_rx(void *context) { (void)context; return BZM_OK; }
bzm_status_t fixture_serial_prepare(int baud)
{ CHECK(baud == BZM_BAUD_RATE); return wait_tx(NULL, 1000); }
bzm_status_t fixture_reset(bool released) { (void)released; return BZM_OK; }
bzm_status_t fixture_pulse(void) { CHECK(fixture.tx_done <= fixture.now); return BZM_OK; }
bzm_bringup_outcome_t fixture_chip_start(bzm_bringup_state_t *state, const bzm_bringup_ops_t *ops,
        void *context, const bzm_bringup_telemetry_policy_t *policy, bzm_bringup_report_t *report)
{
    (void)ops; (void)context; (void)policy; (void)report;
    *state = (bzm_bringup_state_t){.running = true, .clock_mhz = 800};
    return BZM_BRINGUP_GOOD;
}
bzm_bringup_outcome_t fixture_clock_step(bzm_bringup_state_t *state, const bzm_bringup_ops_t *ops,
        void *context, const float target[BZM_BRINGUP_ASIC_COUNT][BZM_BRINGUP_PLL_COUNT],
        bool initial_jump, bzm_bringup_report_t *report)
{
    (void)ops; (void)context; (void)initial_jump; (void)report;
    CHECK(fixture.tx_done <= fixture.now);
    ++fixture.clock_steps;
    memcpy(state->domain_clock_mhz, target, sizeof(state->domain_clock_mhz));
    return BZM_BRINGUP_GOOD;
}

static void submit(bzm_device_t *device, asic_job_t *job)
{
    CHECK(bzm_driver_job_frequency_ms(device) == 0);
    unsigned sleeps = fixture.sleeps, polls = fixture.polls, writes = fixture.writes;
    uint16_t engine = REACTOR.next_engine;
    fixture.now += 1000; // Generate the next header while UART sends the previous one.
    ++job->merkle_root[0];
    fixture.dispatching = true;
    bzm_driver_submit_job(device, job);
    fixture.dispatching = false;
    CHECK(!bzm_driver_has_io_fault());
    CHECK(fixture.sleeps == sleeps && fixture.polls == polls + 1 && fixture.writes > writes);
    CHECK(REACTOR.next_engine == (engine + 1) % BZM_ENGINE_ROWS);
    CHECK(REACTOR.assignments[engine].active);
}

static bzm_raw_result_t response(size_t engine)
{
    const bzm_assignment_t *assignment = &REACTOR.assignments[engine];
    return (bzm_raw_result_t){.asic_id = BZM_FIRST_ASIC_ID,
        .engine_id = assignment->engine_id, .sequence_id = assignment->logical_sequence << 2,
        .status = 8, .time = REACTOR.config.timestamp_count};
}

int main(void)
{
    bzm_platform_ops_t platform;
    bzm_platform_posix_defaults(&platform);
    platform.now_us = now_us;
    platform.sha256 = hash_bytes;
    platform.sleep_us = sleep_us;
    platform.serial_read = read_bytes;
    platform.serial_write = write_bytes;
    platform.serial_wait_tx = wait_tx;
    platform.serial_clear_rx = clear_rx;
    CHECK(bzm_platform_bind(&platform, NULL));
    fixture.allowed = true;
    bzm_device_t device = {.asic_count = 4, .core_count = 236, .initialized = true,
        .can_submit = authorize, .request_work = request_work, .context = &fixture};
    CHECK(bzm_driver_job_frequency_ms(&device) > 0);
    CHECK(bzm_driver_state_init(&device));
    CHECK(bzm_driver_job_frequency_ms(&device) > 0);
    bzm_driver_set_operation_authorizer(authorize, NULL);
    bzm_bringup_telemetry_policy_t policy = {0};
    bzm_bringup_report_t report;
    CHECK(bzm_driver_start(&device, &policy, &report) == BZM_BRINGUP_GOOD);
    CHECK(REACTOR.config.timestamp_count == 127);
    CHECK(fixture.requests == 1);
    CHECK(fixture.nonce_writes == 2 * 4 * BZM_ENGINES_PER_ASIC);
    fixture.bytes = 0;
    bzm_driver_set_dispatch_authorizer(authorize, NULL);
    asic_job_t job = {.version = 0x20000000, .ntime = 1234, .nbits = 0x1702369d, .version_mask = 0x6000};
    int64_t started = fixture.now;
    unsigned drains = fixture.drains;
    for (size_t i = 0; i < BZM_ENGINE_ROWS; ++i) {
        submit(&device, &job);
        if (i < BZM_ENGINE_ROWS - 1) CHECK(fixture.tx_done > fixture.now);
    }
    CHECK(fixture.bytes == BZM_ENGINE_ROWS * 462);
    CHECK(fixture.drains == drains + 1 && fixture.tx_done == fixture.now);
    CHECK(fixture.now - started == 1000 + BZM_ENGINE_ROWS * 2310);
    CHECK(bzm_driver_job_frequency_ms(&device) > 1700);
    CHECK(bzm_driver_job_frequency_ms(&device) < 1720);
    // Every row must renew before its shortest partition runs out, including
    // the first staggered cycle after an immediate full refill.
    const int64_t lifetime_us = (int64_t)(128 * 3 * 4294967296.0 / 800 / 48);
    int64_t last_row[BZM_ENGINE_ROWS];
    for (size_t i = 0; i < BZM_ENGINE_ROWS; ++i) last_row[i] = started;
    for (size_t i = 0; i < 3 * BZM_ENGINE_ROWS; ++i) {
        fixture.now = BZM_STATE->refresh_at_us - 1;
        CHECK(bzm_driver_job_frequency_ms(&device) == 1);
        ++fixture.now;
        CHECK(bzm_driver_job_frequency_ms(&device) == 0);
        size_t row = REACTOR.next_engine;
        size_t bytes = fixture.bytes;
        unsigned before_drains = fixture.drains;
        submit(&device, &job);
        CHECK(fixture.now - last_row[row] < lifetime_us * 85 / 100);
        last_row[row] = fixture.now;
        CHECK(fixture.bytes == bytes + 462 && fixture.drains == before_drains + 1);
        CHECK(fixture.tx_done == fixture.now && REFILL_REMAINING == 0);
        CHECK(bzm_driver_job_frequency_ms(&device) > 1710);
        CHECK(bzm_driver_job_frequency_ms(&device) < 1720);
    }

    bzm_raw_result_t old[BZM_ENGINE_ROWS];
    for (size_t i = 0; i < BZM_ENGINE_ROWS; ++i) old[i] = response(i);
    bzm_result_t result;
    CHECK(bzm_reactor_map_result(&REACTOR, &old[0], &result));
    CHECK(bzm_driver_clear_work(&device));
    for (size_t i = 0; i < 17; ++i) submit(&device, &job);
    CHECK(bzm_driver_clear_work(&device));
    for (size_t i = 0; i < BZM_ENGINE_ROWS; ++i) {
        CHECK(!bzm_reactor_map_result(&REACTOR, &old[i], &result));
        submit(&device, &job);
    }
    CHECK(REACTOR.next_engine == 17);
    CHECK(bzm_driver_job_frequency_ms(&device) > 1700);

    // A clean job can preempt software validation after a result was mapped.
    TRANSPORT.io.pending_result_head = 0;
    TRANSPORT.io.pending_result_length = 1;
    TRANSPORT.io.pending_results[0] = response(0);
    CHECK(bzm_driver_process_result(&device) != NULL);
    TRANSPORT.io.pending_result_head = 0;
    TRANSPORT.io.pending_result_length = 1;
    TRANSPORT.io.pending_results[0] = response(0);
    ++TRANSPORT.io.pending_results[0].nonce;
    fixture.retire_during_hash = true;
    CHECK(bzm_driver_process_result(&device) == NULL);
    CHECK(!fixture.retire_during_hash);

    float targets[BZM_BRINGUP_ASIC_COUNT][BZM_BRINGUP_PLL_COUNT];
    for (size_t chip = 0; chip < BZM_BRINGUP_ASIC_COUNT; ++chip)
        for (size_t pll = 0; pll < BZM_BRINGUP_PLL_COUNT; ++pll)
            targets[chip][pll] = chip == 3 && pll == 1 ? 1600 : 800;
    CHECK(bzm_driver_clear_work(&device));
    submit(&device, &job);
    CHECK(fixture.tx_done > fixture.now);
    CHECK(bzm_driver_step_frequency_domains((const float (*)[BZM_BRINGUP_PLL_COUNT])targets,
        false, &report, NULL) == BZM_BRINGUP_GOOD);
    CHECK(fixture.requests == 2);
    uint32_t generation, completed;
    bool pending;
    for (size_t i = 0; i < BZM_ENGINE_ROWS; ++i) {
        if (i % 3 == 0) CHECK(bzm_driver_clear_work(&device));
        submit(&device, &job);
    }
    CHECK(bzm_driver_work_replacement_snapshot(&generation, &completed, &pending));
    CHECK(generation == 2 && completed == generation && !pending);
    CHECK(fixture.tx_done == fixture.now); // Progress completion survives repeated clean jobs.
    while (REFILL_REMAINING) submit(&device, &job);
    double delay = bzm_driver_job_frequency_ms(&device);
    CHECK(delay > 830 && delay < 860);
    fixture.allowed = false;
    unsigned writes = fixture.writes;
    bzm_driver_submit_job(&device, &job);
    CHECK(fixture.writes == writes && bzm_driver_job_frequency_ms(&device) > 0);
    fixture.allowed = true;
    CHECK(bzm_driver_clear_work(&device));
    submit(&device, &job);
    CHECK(fixture.tx_done > fixture.now);
    CHECK(bzm_driver_hold_reset());
    CHECK(fixture.tx_done > fixture.now); // Assert reset immediately, even with queued TX.
    CHECK(bzm_driver_start(&device, &policy, &report) == BZM_BRINGUP_GOOD);
    bzm_driver_set_dispatch_authorizer(authorize, NULL);
    CHECK(fixture.tx_done <= fixture.now); // Session preparation drains before reset release.
    for (size_t i = 0; i < BZM_ENGINE_ROWS - 1; ++i) submit(&device, &job);
    CHECK(bzm_driver_work_replacement_snapshot(&generation, &completed, &pending));
    CHECK(pending && generation != completed);
    fixture.fail_drain = true;
    CHECK(!driver_send_work(&device, &job));
    CHECK(bzm_driver_has_io_fault());
    CHECK(bzm_driver_work_replacement_snapshot(&generation, &completed, &pending));
    CHECK(pending && generation != completed);
    fixture.fail_drain = false;
    CHECK(bzm_driver_start(&device, &policy, &report) == BZM_BRINGUP_GOOD);
    bzm_driver_set_dispatch_authorizer(authorize, NULL);
    submit(&device, &job);
    unsigned clock_steps = fixture.clock_steps;
    fixture.fail_drain = true;
    CHECK(bzm_driver_step_frequency_domains((const float (*)[BZM_BRINGUP_PLL_COUNT])targets,
        false, &report, NULL) == BZM_BRINGUP_BAD);
    CHECK(report.reason == BZM_BRINGUP_REASON_IO && fixture.clock_steps == clock_steps);
    CHECK(bzm_driver_has_io_fault());
    writes = fixture.writes;
    device.initialized = false;
    CHECK(bzm_driver_job_frequency_ms(&device) > 0);
    device.initialized = true;
    CHECK(bzm_driver_job_frequency_ms(&device) > 0);
    CHECK(!driver_send_work(&device, &job));
    CHECK(fixture.writes == writes);
    fixture.fail_drain = false;
    fixture.fail_nonce = true;
    CHECK(bzm_driver_start(&device, &policy, &report) == BZM_BRINGUP_BAD);
    CHECK(!INITIALIZED && !DRIVER_TRANSPORT_READY && !DRIVER_BRINGUP.running);
    CHECK(report.reason == BZM_BRINGUP_REASON_IO);
    bzm_serial_transport_deinit(&TRANSPORT);
    bzm_job_store_destroy(&BZM_STATE->job_store);
    CHECK(pthread_mutex_destroy(&REACTOR_LOCK) == 0);
    bzm_free(BZM_STATE);
    BZM_STATE = NULL;
    return 0;
}
