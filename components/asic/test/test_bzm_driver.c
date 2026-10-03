#include "bzm_test_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BZM_driver_state_init test_driver_state_init
#define BZM_set_max_baud test_driver_set_max_baud
#define BZM_submit_job test_driver_submit_job
#define BZM_clear_work test_driver_clear_work
#define BZM_job_frequency_ms test_driver_job_frequency_ms
#define BZM_process_work test_driver_process_work
#define BZM_read_temperature test_driver_read_temperature
#define BZM_hashrate_counter_snapshot test_driver_hashrate_snapshot
#define BZM_has_io_fault test_driver_has_io_fault
#define BZM_work_replacement_snapshot test_driver_replacement_snapshot
#define BZM_get_telemetry_snapshot test_driver_telemetry_snapshot
#define BZM_step_frequency_domains test_driver_step_frequency_domains
#define BZM_start test_driver_start
#define BZM_get_state test_driver_get_state
#define BZM_hold_reset test_driver_hold_reset
#define BZM_set_dispatch_authorizer test_driver_dispatch_authorizer
#define BZM_set_operation_authorizer test_driver_operation_authorizer
#define BZM_poll test_driver_poll
#define bzm_serial_read_result test_driver_read_result
#define mining_nonce_difficulty test_driver_difficulty

#define BZM_bridge_safety_heartbeat test_driver_heartbeat
#define BZM_bridge_set_asic_reset test_driver_reset
#define BZM_bridge_pulse_asic_reset test_driver_pulse
#define BZM_SERIAL_prepare_session test_driver_serial_prepare
#define bzm_bringup_start test_driver_chip_start
#define esp_psram_is_initialized test_driver_psram_ready
#include "../bzm_driver.c"
#include "unity.h"

static bool authorized, fail_write, cancel_write, fail_flush;
static double result_difficulty;
static bzm_work_t last_work;
static bzm_raw_result_t raw_result;
static GlobalState test_state;
static unsigned startup_failure, startup_reset_calls, startup_chip_calls;
static bool startup_reset_asserted;
bool test_driver_psram_ready(void) { return false; }

esp_err_t test_driver_heartbeat(bzm_bridge_safety_status_t *status)
{
    *status = (bzm_bridge_safety_status_t){
        .valid = true, .state = BZM_BRIDGE_SAFETY_STATE_CONTROLLED,
        .fault = BZM_BRIDGE_SAFETY_FAULT_NONE,
        .runtime_verdict = BZM_BRIDGE_SAFETY_RUNTIME_GOOD_CONTROLLED,
        .lease_remaining_ms = 2000,
        .evidence = BZM_BRIDGE_SAFETY_EVIDENCE_LEASE_VALID |
                    BZM_BRIDGE_SAFETY_EVIDENCE_TRIP_CLEAR | BZM_BRIDGE_SAFETY_EVIDENCE_FAULT_CLEAR,
    };
    return startup_failure == 1 ? ESP_FAIL : ESP_OK;
}
esp_err_t test_driver_reset(bool released)
{
    ++startup_reset_calls;
    if (startup_failure == 2) return ESP_FAIL;
    startup_reset_asserted = !released;
    return ESP_OK;
}
esp_err_t test_driver_pulse(void)
{
    startup_reset_asserted = false;
    return startup_failure == 4 ? ESP_FAIL : ESP_OK;
}
esp_err_t test_driver_serial_prepare(int baud)
{
    TEST_ASSERT_EQUAL_INT(BZM_BAUD_RATE, baud);
    return startup_failure == 3 ? ESP_FAIL : ESP_OK;
}
bzm_bringup_outcome_t test_driver_chip_start(bzm_bringup_state_t *state, const bzm_bringup_ops_t *ops,
                                             void *context, const bzm_bringup_telemetry_policy_t *policy,
                                             bzm_bringup_report_t *report)
{
    (void)ops; (void)context; (void)policy;
    ++startup_chip_calls;
    TEST_ASSERT_FALSE(bzm_dispatch_gate_is_authorized(&DRIVER_DISPATCH_GATE));
    if (startup_failure == 5) {
        DRIVER_BALANCED_RAMP = (bzm_balanced_ramp_report_t){
            .failure = BZM_BALANCED_RAMP_FAILURE_CONFIG_READBACK, .failure_asic_id = 0x14,
            .failure_register_offset = BZM_ENGINE_REG_CONFIG, .failure_expected = 4, .failure_actual = 0,
        };
        *report = (bzm_bringup_report_t){.reason = BZM_BRINGUP_REASON_BALANCED_PAIR_COMMIT};
        return BZM_BRINGUP_BAD;
    }
    *state = (bzm_bringup_state_t){.running = true, .clock_mhz = 800};
    *report = (bzm_bringup_report_t){0};
    return BZM_BRINGUP_GOOD;
}

static bool allow_dispatch(void *context) { (void)context; return authorized; }
static bool write_work(void *context, const bzm_work_t *work)
{
    (void)context;
    last_work = *work;
    if (cancel_write) authorized = false;
    return !fail_write && !cancel_write;
}
static bool flush(void *context) { (void)context; return !fail_flush; }
bool test_driver_read_result(bzm_serial_transport_t *transport, bzm_raw_result_t *result, uint16_t timeout_ms)
{
    (void)transport; (void)timeout_ms;
    *result = raw_result;
    return true;
}
double test_driver_difficulty(const asic_job_t *job, uint32_t nonce, uint32_t version)
{
    (void)job; (void)nonce; (void)version;
    return result_difficulty;
}

static void fixture_init(void)
{
    BZM_STATE = calloc(1, sizeof(*BZM_STATE));
    TEST_ASSERT_NOT_NULL(BZM_STATE);
    TEST_ASSERT_TRUE(bzm_job_store_init(&BZM_STATE->job_store));
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_init(&REACTOR_LOCK, NULL));
    atomic_init(&BZM_STATE->work_epoch, 0);
    atomic_init(&BZM_STATE->io_failed, false);
    atomic_init(&WORK_REPLACEMENT_GENERATION, 0);
    for (size_t i = 0; i < BZM_MAX_ASIC_COUNT; ++i) atomic_init(&HASHRATE_DIFFICULTY_ONE_COUNTERS[i], 0);
    bzm_reactor_config_t config = {.engine_count = BZM_ENGINES_PER_ASIC, .timestamp_count = 16,
                                   .nonce_offset = BZM_NONCE_GAP_1002};
    bzm_transport_ops_t ops = {.write_work = write_work, .flush = flush};
    TEST_ASSERT_TRUE(bzm_reactor_init(&REACTOR, &BZM_STATE->job_store, &config, &ops, NULL));
    INITIALIZED = DRIVER_TRANSPORT_READY = DRIVER_BRINGUP.running = true;
    BZM_set_dispatch_authorizer(allow_dispatch, NULL);
    authorized = true;
    fail_write = cancel_write = fail_flush = false;
    startup_failure = startup_reset_calls = startup_chip_calls = 0;
    startup_reset_asserted = false;
    memset(&test_state, 0, sizeof(test_state));
    test_state.DEVICE_CONFIG.family = FAMILY_BONANZA;
    test_state.ASIC_initalized = true;
}
static void fixture_destroy(void)
{
    bzm_serial_transport_deinit(&TRANSPORT);
    bzm_job_store_destroy(&BZM_STATE->job_store);
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_destroy(&REACTOR_LOCK));
    free(BZM_STATE);
    BZM_STATE = NULL;
}
static asic_job_t template(void)
{
    return (asic_job_t){.version = 0x20000000, .ntime = 1234, .nbits = 0x1702369d,
                        .version_mask = 0x6000, .pool_diff = 1};
}

TEST_CASE("BZM clean jobs restart a complete fast replacement rotation", "[asic][bzm][driver]")
{
    fixture_init();
    asic_job_t job = template();
    TEST_ASSERT_TRUE(BZM_clear_work(&test_state));
    for (size_t i = 0; i < BZM_ENGINES_PER_ASIC - 1; ++i) TEST_ASSERT_TRUE(BZM_send_work(&test_state, &job));
    TEST_ASSERT_TRUE(BZM_clear_work(&test_state));
    uint32_t generation, completed;
    bool pending;
    for (size_t i = 0; i < BZM_ENGINES_PER_ASIC - 1; ++i) {
        TEST_ASSERT_TRUE(BZM_send_work(&test_state, &job));
        TEST_ASSERT_EQUAL_DOUBLE(10.0, BZM_job_frequency_ms(&test_state));
    }
    TEST_ASSERT_TRUE(BZM_send_work(&test_state, &job));
    TEST_ASSERT_TRUE(BZM_work_replacement_snapshot(&generation, &completed, &pending));
    TEST_ASSERT_FALSE(pending);
    TEST_ASSERT_EQUAL_UINT32(generation, completed);
    TEST_ASSERT_EQUAL_DOUBLE(100.0, BZM_job_frequency_ms(&test_state));
    fixture_destroy();
}

TEST_CASE("BZM frequent clean jobs cannot starve clock-change synchronization", "[asic][bzm][driver]")
{
    fixture_init();
    asic_job_t job = template();
    start_work_replacement_locked(BZM_ENGINES_PER_ASIC);
    uint32_t generation, completed;
    bool pending;
    for (size_t i = 0; i < BZM_ENGINES_PER_ASIC; ++i) {
        if (i % 25 == 0) TEST_ASSERT_TRUE(BZM_clear_work(&test_state));
        TEST_ASSERT_TRUE(BZM_work_replacement_snapshot(&generation, &completed, &pending));
        TEST_ASSERT_TRUE(pending);
        TEST_ASSERT_TRUE(BZM_send_work(&test_state, &job));
    }
    TEST_ASSERT_TRUE(BZM_work_replacement_snapshot(&generation, &completed, &pending));
    TEST_ASSERT_FALSE(pending);
    TEST_ASSERT_EQUAL_UINT32(1, generation);
    TEST_ASSERT_EQUAL_UINT32(generation, completed);
    TEST_ASSERT_EQUAL_DOUBLE(10.0, BZM_job_frequency_ms(&test_state));
    fixture_destroy();
}

TEST_CASE("BZM real transport errors latch but withdrawn dispatch does not", "[asic][bzm][driver]")
{
    for (unsigned failure = 0; failure < 3; ++failure) {
        fixture_init();
        asic_job_t job = template();
        fail_write = failure == 0;
        cancel_write = failure == 1;
        if (failure == 2) {
            REACTOR.next_engine_sequence[0] = 63;
            fail_flush = true;
        }
        TEST_ASSERT_FALSE(BZM_send_work(&test_state, &job));
        TEST_ASSERT_EQUAL(failure != 1, BZM_has_io_fault());
        fixture_destroy();
    }
}

TEST_CASE("BZM difficulty filtering rejects nonfinite results and credits only valid hashes", "[asic][bzm][driver]")
{
    fixture_init();
    asic_job_t job = template();
    TEST_ASSERT_TRUE(BZM_send_work(&test_state, &job));
    raw_result = (bzm_raw_result_t){.asic_id = BZM_FIRST_ASIC_ID, .engine_id = last_work.engine_id,
                                   .status = 8, .time = 16, .sequence_id = 0};
    const double cases[] = {NAN, INFINITY, -INFINITY, 0.0, 15.99, 16.0, 32.0};
    uint32_t counters[BZM_MAX_ASIC_COUNT] = {0};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        result_difficulty = cases[i];
        ++raw_result.nonce;
        task_result *result = BZM_process_work(&test_state);
        TEST_ASSERT_EQUAL(i >= 5, result != NULL);
    }
    TEST_ASSERT_TRUE(BZM_hashrate_counter_snapshot(&test_state, counters, BZM_MAX_ASIC_COUNT));
    TEST_ASSERT_EQUAL_UINT32(32, counters[0]);
    TEST_ASSERT_EQUAL_UINT32(0, counters[1]);
    // A duplicate valid result must not earn a second hashrate credit.
    TEST_ASSERT_NULL(BZM_process_work(&test_state));
    TEST_ASSERT_TRUE(BZM_hashrate_counter_snapshot(&test_state, counters, BZM_MAX_ASIC_COUNT));
    TEST_ASSERT_EQUAL_UINT32(32, counters[0]);
    fixture_destroy();
}

TEST_CASE("BZM startup closes dispatch and attempts reset after transport or chip failure", "[asic][bzm][driver][startup]")
{
    for (unsigned failure = 1; failure <= 5; ++failure) {
        fixture_init();
        startup_failure = failure;
        BZM_set_operation_authorizer(allow_dispatch, NULL);
        bzm_bringup_report_t report;
        bzm_bringup_telemetry_policy_t policy = {0};
        TEST_ASSERT_EQUAL(BZM_BRINGUP_BAD, BZM_start(&test_state, &policy, &report));
        TEST_ASSERT_FALSE(INITIALIZED);
        TEST_ASSERT_FALSE(DRIVER_TRANSPORT_READY);
        TEST_ASSERT_FALSE(DRIVER_BRINGUP.running);
        TEST_ASSERT_FALSE(bzm_dispatch_gate_is_authorized(&DRIVER_DISPATCH_GATE));
        TEST_ASSERT_EQUAL_UINT32(failure == 1 ? 1 : 2, startup_reset_calls);
        TEST_ASSERT_EQUAL(failure != 2, startup_reset_asserted);
        TEST_ASSERT_EQUAL_UINT32(failure == 5 ? 1 : 0, startup_chip_calls);
        if (failure == 5) {
            TEST_ASSERT_EQUAL(BZM_BRINGUP_REASON_BALANCED_PAIR_COMMIT, report.reason);
            TEST_ASSERT_EQUAL_HEX8(0x14, report.asic_id);
            TEST_ASSERT_EQUAL_HEX8(BZM_ENGINE_REG_CONFIG, report.register_offset);
            TEST_ASSERT_EQUAL_UINT32(4, report.expected);
        }
        TEST_ASSERT_EQUAL_INT(0, pthread_mutex_trylock(&REACTOR_LOCK));
        pthread_mutex_unlock(&REACTOR_LOCK);
        fixture_destroy();
    }
}

TEST_CASE("BZM startup primes every engine and waits for board dispatch authorization", "[asic][bzm][driver][startup]")
{
    fixture_init();
    BZM_set_operation_authorizer(allow_dispatch, NULL);
    bzm_bringup_report_t report;
    bzm_bringup_telemetry_policy_t policy = {0};
    TEST_ASSERT_EQUAL(BZM_BRINGUP_GOOD, BZM_start(&test_state, &policy, &report));
    TEST_ASSERT_TRUE(INITIALIZED);
    TEST_ASSERT_TRUE(DRIVER_TRANSPORT_READY);
    TEST_ASSERT_TRUE(DRIVER_BRINGUP.running);
    TEST_ASSERT_FALSE(startup_reset_asserted);
    TEST_ASSERT_EQUAL_UINT32(1, startup_chip_calls);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bzm_asic_wire_ids, TRANSPORT.asic_ids, BZM_MAX_ASIC_COUNT);
    TEST_ASSERT_EQUAL_UINT16(BZM_ENGINES_PER_ASIC, FAST_DISPATCH_REMAINING);
    TEST_ASSERT_EQUAL_UINT16(BZM_ENGINES_PER_ASIC, BZM_STATE->work_replacement_remaining);
    TEST_ASSERT_FALSE(bzm_dispatch_gate_is_authorized(&DRIVER_DISPATCH_GATE));
    BZM_set_dispatch_authorizer(allow_dispatch, NULL);
    TEST_ASSERT_TRUE(bzm_dispatch_gate_is_authorized(&DRIVER_DISPATCH_GATE));
    fixture_destroy();
}
