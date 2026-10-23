#include "check.h"

#define bzm_port_serial_prepare_session fixture_serial_prepare
#define bzm_control_set_asic_reset fixture_reset
#define bzm_control_pulse_asic_reset fixture_pulse
#define bzm_bringup_start fixture_chip_start
#include "../../../bzm/bzm_driver.c"

static struct {
    int64_t now;
    bool read_error;
    bool byte_before_error;
    bool exercise_startup, fail_tdm_once, fail_next_read, reply_to_registers, reset_asserted;
    uint8_t reply[6];
    size_t reply_length, reply_cursor;
    uint32_t tdm_control;
    unsigned writes, tdm_reads, activation_writes, read_errors;
    unsigned reads;
    unsigned faults;
    unsigned sleeps;
} fixture;

static int64_t now_us(void *context) { (void)context; return fixture.now; }
static void sleep_us(void *context, uint32_t us)
{
    (void)context;
    fixture.now += us;
    ++fixture.sleeps;
}
static bool authorize(void *context) { (void)context; return true; }
static int16_t read_bytes(void *context, uint8_t *data, uint16_t size, uint16_t timeout_ms)
{
    (void)context;
    CHECK(size != 0);
    ++fixture.reads;
    if (fixture.fail_next_read) {
        fixture.fail_next_read = false;
        ++fixture.read_errors;
        return -1;
    }
    if (fixture.byte_before_error) {
        fixture.byte_before_error = false;
        data[0] = BZM_FIRST_ASIC_ID;
        return 1;
    }
    if (fixture.read_error) return -1;
    if (fixture.reply_cursor < fixture.reply_length) {
        size_t count = fixture.reply_length - fixture.reply_cursor;
        if (count > size) count = size;
        memcpy(data, fixture.reply + fixture.reply_cursor, count);
        fixture.reply_cursor += count;
        return (int16_t)count;
    }
    fixture.now += (int64_t)timeout_ms * 1000;
    return 0;
}
static bzm_status_t wait_tx(void *context, uint32_t timeout_ms)
{
    (void)context; (void)timeout_ms;
    return BZM_OK;
}
static int write_bytes(void *context, const uint8_t *data, size_t size)
{
    (void)context;
    ++fixture.writes;
    if (fixture.reply_to_registers && size >= 12) {
        uint16_t engine = ((uint16_t)(data[2] & 0x0f) << 8) | data[4];
        uint8_t offset = data[6];
        bool control = engine == BZM_BRINGUP_CONTROL_ENGINE_ID;
        if ((data[2] & 0xf0) == 0x30) {
            CHECK(data[8] == 3);
            if (control && offset == BZM_LOCAL_REG_UART_TDM_CONTROL) {
                ++fixture.tdm_reads;
                if (fixture.fail_tdm_once) {
                    fixture.fail_tdm_once = false;
                    fixture.fail_next_read = true;
                    return (int)size;
                }
            }
            uint32_t value = control ? fixture.tdm_control :
                BZM_BALANCED_RAMP_ENGINE_BUSY_MASK | (BZM_BALANCED_RAMP_ENGINE_CONFIG << 8);
            fixture.reply[0] = data[0];
            fixture.reply[1] = BZM_FRAME_REGISTER;
            for (unsigned i = 0; i < 4; ++i) fixture.reply[i + 2] = (uint8_t)(value >> (8 * i));
            fixture.reply_cursor = 0;
            fixture.reply_length = sizeof(fixture.reply);
        } else if ((data[2] & 0xf0) == 0x20) {
            if (control && offset == BZM_LOCAL_REG_UART_TDM_CONTROL) {
                CHECK(size >= 18);
                fixture.tdm_control = (uint32_t)data[10] | ((uint32_t)data[12] << 8) |
                    ((uint32_t)data[14] << 16) | ((uint32_t)data[16] << 24);
            } else if (!control && offset == BZM_ENGINE_REG_CONFIG) {
                ++fixture.activation_writes;
            }
        }
    }
    return (int)size;
}
static bzm_status_t clear_rx(void *context)
{
    (void)context;
    fixture.reply_length = fixture.reply_cursor = 0;
    return BZM_OK;
}
static void report_fault(void *context, const char *reason)
{
    (void)context;
    CHECK(strcmp(reason, "BZM transport failed") == 0);
    ++fixture.faults;
}

bzm_status_t fixture_serial_prepare(int baud)
{
    CHECK(baud == BZM_BAUD_RATE);
    return BZM_OK;
}
bzm_status_t fixture_reset(bool released) { fixture.reset_asserted = !released; return BZM_OK; }
bzm_status_t fixture_pulse(void) { fixture.reset_asserted = false; return BZM_OK; }
bzm_bringup_outcome_t fixture_chip_start(bzm_bringup_state_t *state, const bzm_bringup_ops_t *ops,
                                        void *context, const bzm_bringup_telemetry_policy_t *policy,
                                        bzm_bringup_report_t *report)
{
    (void)policy;
    if (fixture.exercise_startup) {
        CHECK(pthread_mutex_trylock(&REACTOR_LOCK) != 0);
        for (unsigned i = 0; i < BZM_BRINGUP_ASIC_COUNT; ++i) {
            TRANSPORT.io.telemetry.samples[i] = (bzm_telemetry_sample_t){
                .asic_id = bzm_asic_wire_ids[i], .received = true, .valid = true,
                .thermal_valid = true, .voltage_enabled = true, .voltage_valid = true,
                .pll_locked = true, .timestamp_us = (uint64_t)fixture.now,
                .temperature_c = 40, .ch0_mv = 500, .ch1_mv = 500,
            };
        }
        bzm_engine_pair_t pair;
        CHECK(bzm_topology_balanced_pair_at(1, &pair));
        if (!ops->balanced_batch_begin(context, 1) ||
            !ops->balanced_pair_commit(context, BZM_FIRST_ASIC_ID, &pair)) {
            *report = (bzm_bringup_report_t){.reason = BZM_BRINGUP_REASON_IO};
            return BZM_BRINGUP_BAD;
        }
    }
    *state = (bzm_bringup_state_t){.running = true, .clock_mhz = 800};
    *report = (bzm_bringup_report_t){0};
    return BZM_BRINGUP_GOOD;
}

static bzm_device_t device = {
    .asic_count = BZM_BRINGUP_ASIC_COUNT,
    .core_count = BZM_ENGINES_PER_ASIC,
    .initialized = true,
    .report_fault = report_fault,
};

static void start_driver(void)
{
    memset(&fixture, 0, sizeof(fixture));
    CHECK(bzm_driver_state_init(&device));
    bzm_driver_set_operation_authorizer(authorize, NULL);
    const bzm_bringup_telemetry_policy_t policy = {0};
    bzm_bringup_report_t report;
    CHECK(bzm_driver_start(&device, &policy, &report) == BZM_BRINGUP_GOOD);
    bzm_driver_set_dispatch_authorizer(authorize, NULL);
    CHECK(!bzm_driver_has_io_fault());
    fixture.reads = 0;
}

static void test_receive_faults(void)
{
    for (unsigned path = 0; path < 5; ++path) {
        start_driver();
        fixture.read_error = true;
        if (path == 0) {
            bzm_driver_service(&device);
            CHECK(fixture.faults == 1);
        } else if (path == 1) {
            CHECK(bzm_driver_process_result(&device) == NULL);
        } else if (path == 2) {
            CHECK(!driver_mining_dispatch_checkpoint(&TRANSPORT));
        } else if (path == 3) {
            CHECK(driver_probe_noop(&TRANSPORT, BZM_FIRST_ASIC_ID) == BZM_BRINGUP_PROBE_IO_ERROR);
        } else {
            uint32_t value;
            CHECK(!driver_read_u32(&TRANSPORT, BZM_FIRST_ASIC_ID, 0, 0, &value));
        }
        CHECK(fixture.reads == 1);
        CHECK(bzm_driver_has_io_fault());
        fixture.read_error = false;
        CHECK(bzm_driver_poll(1) == 0);
        CHECK(bzm_serial_poll(&TRANSPORT, 1).status == BZM_OK);
        CHECK(bzm_driver_has_io_fault());
        fixture.faults = 0;
        bzm_driver_service(&device);
        CHECK(fixture.faults == 1);
    }
}

static void test_drain_fault(void)
{
    start_driver();
    fixture.byte_before_error = fixture.read_error = true;
    bzm_driver_service(&device);
    CHECK(fixture.reads == 2);
    CHECK(bzm_driver_has_io_fault());
    CHECK(fixture.faults == 1);
}

static void test_timeouts_and_restart(void)
{
    start_driver();
    CHECK(bzm_driver_poll(1) == 0);
    CHECK(bzm_driver_process_result(&device) == NULL);
    bzm_driver_service(&device);
    CHECK(!bzm_driver_has_io_fault());
    CHECK(fixture.faults == 0);
    fixture.byte_before_error = true;
    CHECK(bzm_driver_poll(1) == 0);
    CHECK(TRANSPORT.io.parser.buffered_length == 1);
    bzm_driver_service(&device);
    CHECK(!bzm_driver_has_io_fault());
    CHECK(fixture.faults == 0);
}

static void test_stopped_receiver_waits_for_shared_stop(void)
{
    start_driver();
    CHECK(bzm_driver_hold_reset());
    CHECK(device.initialized && fixture.reset_asserted);
    unsigned reads = fixture.reads, writes = fixture.writes, sleeps = fixture.sleeps;
    int64_t started = fixture.now;
    for (unsigned i = 0; i < 100; ++i) CHECK(bzm_driver_process_result(&device) == NULL);
    CHECK(fixture.reads == reads && fixture.writes == writes);
    CHECK(fixture.sleeps == sleeps + 100 && fixture.now >= started + 100000);
    puts("stopped receiver: 100 calls wait without UART access while shared state remains initialized");
}

static void test_startup_read_error_stops_activation(void)
{
    const bzm_bringup_telemetry_policy_t policy = {
        .bounds = {
            .temperature_min_c = -20, .temperature_max_c = 75,
            .ch0_min_mv = 300, .ch0_max_mv = 800,
            .ch1_min_mv = 300, .ch1_max_mv = 800,
            .ch2_abs_max_mv = 50, .max_stack_spread_mv = 100,
        },
        .max_age_us = BZM_TELEMETRY_MAX_AGE_US, .ch2_confirm_samples = 1,
    };
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        memset(&fixture, 0, sizeof(fixture));
        fixture.exercise_startup = fixture.reply_to_registers = true;
        fixture.fail_tdm_once = attempt == 0;
        CHECK(bzm_driver_state_init(&device));
        bzm_driver_set_operation_authorizer(authorize, NULL);
        bzm_bringup_report_t report;
        bzm_bringup_outcome_t result = bzm_driver_start(&device, &policy, &report);
        printf("startup attempt=%u result=%d RX errors=%u TDM reads=%u activation writes=%u fault=%u reset=%u\n",
               attempt, result, fixture.read_errors, fixture.tdm_reads, fixture.activation_writes,
               bzm_driver_has_io_fault(), fixture.reset_asserted);
        if (attempt == 0) {
            CHECK(result == BZM_BRINGUP_BAD);
            CHECK(fixture.read_errors == 1 && fixture.tdm_reads == 1);
            CHECK(fixture.activation_writes == 0);
            CHECK(bzm_driver_has_io_fault() && fixture.reset_asserted);
            CHECK(!INITIALIZED && !DRIVER_BRINGUP.running);
        } else {
            CHECK(result == BZM_BRINGUP_GOOD);
            CHECK(fixture.read_errors == 0 && fixture.tdm_reads == BZM_BRINGUP_ASIC_COUNT);
            CHECK(fixture.activation_writes == 2);
            CHECK(!bzm_driver_has_io_fault() && !fixture.reset_asserted);
            CHECK(INITIALIZED && DRIVER_BRINGUP.running);
        }
        CHECK(fixture.faults == 0);
    }
}

static void test_latched_fault_blocks_later_writes(void)
{
    for (unsigned path = 0; path < 7; ++path) {
        start_driver();
        fixture.reply_to_registers = fixture.fail_next_read = true;
        uint32_t value = 0;
        CHECK(!bzm_serial_read_register(&TRANSPORT, BZM_FIRST_ASIC_ID,
            BZM_BRINGUP_CONTROL_ENGINE_ID, BZM_LOCAL_REG_UART_TDM_CONTROL, &value, sizeof(value)));
        CHECK(bzm_driver_has_io_fault());
        CHECK(bzm_serial_read_register(&TRANSPORT, BZM_FIRST_ASIC_ID,
            BZM_BRINGUP_CONTROL_ENGINE_ID, BZM_LOCAL_REG_UART_TDM_CONTROL, &value, sizeof(value)));
        CHECK(bzm_driver_has_io_fault());
        unsigned writes = fixture.writes, reads = fixture.reads;
        asic_job_t job = {.version = 0x20000000, .ntime = 1234, .nbits = 0x1702369d};
        if (path == 0) {
            CHECK(!DRIVER_BALANCED_RAMP_OPS.begin_engine(&TRANSPORT, BZM_FIRST_ASIC_ID, 0));
            uint8_t config = BZM_BALANCED_RAMP_ENGINE_CONFIG;
            CHECK(!DRIVER_BALANCED_RAMP_OPS.write_register(&TRANSPORT, BZM_FIRST_ASIC_ID,
                0, BZM_ENGINE_REG_CONFIG, &config, sizeof(config)));
        } else if (path == 1) {
            CHECK(!DRIVER_LIVE_FREQUENCY_OPS.write_u32(&TRANSPORT, BZM_FIRST_ASIC_ID,
                BZM_BRINGUP_CONTROL_ENGINE_ID, BZM_LOCAL_REG_PLL0_CONTROL, 0));
        } else if (path == 2) {
            bzm_work_t work = {0};
            CHECK(!driver_mining_write_work(&TRANSPORT, &work));
        } else if (path == 3) {
            CHECK(!driver_mining_dispatch_checkpoint(&TRANSPORT));
        } else if (path == 4) {
            float targets[BZM_BRINGUP_ASIC_COUNT][BZM_BRINGUP_PLL_COUNT];
            for (unsigned chip = 0; chip < BZM_BRINGUP_ASIC_COUNT; ++chip)
                for (unsigned pll = 0; pll < BZM_BRINGUP_PLL_COUNT; ++pll) targets[chip][pll] = 825;
            bzm_bringup_report_t report;
            CHECK(bzm_driver_step_frequency_domains((const float (*)[BZM_BRINGUP_PLL_COUNT])targets,
                false, &report, NULL) == BZM_BRINGUP_BAD);
            CHECK(report.reason == BZM_BRINGUP_REASON_PREREQUISITE);
        } else if (path == 5) {
            CHECK(!driver_send_work(&device, &job));
        } else {
            bzm_driver_submit_job(&device, &job);
        }
        CHECK(fixture.writes == writes && fixture.reads == reads);
        CHECK(fixture.activation_writes == 0 && fixture.faults == 0);
    }
    puts("persistent fault guards: 7 paths blocked after a failed read and a successful raw retry");
}

int main(void)
{
    const bzm_platform_ops_t platform = {
        .now_us = now_us, .sleep_us = sleep_us,
        .serial_read = read_bytes, .serial_wait_tx = wait_tx,
        .serial_write = write_bytes, .serial_clear_rx = clear_rx,
    };
    CHECK(bzm_platform_bind(&platform, NULL));
    test_receive_faults();
    test_drain_fault();
    test_timeouts_and_restart();
    test_stopped_receiver_waits_for_shared_stop();
    test_startup_read_error_stops_activation();
    test_latched_fault_blocks_later_writes();
    bzm_serial_transport_deinit(&TRANSPORT);
    bzm_job_store_destroy(&BZM_STATE->job_store);
    CHECK(pthread_mutex_destroy(&REACTOR_LOCK) == 0);
    bzm_free(BZM_STATE);
    BZM_STATE = NULL;
    return 0;
}
