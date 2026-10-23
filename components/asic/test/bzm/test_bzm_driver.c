#include "esp_err.h"
#include "esp_timer.h"
#include "utils.h"
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#define bzm_driver_init test_driver_init
#define bzm_driver_set_frequency test_driver_set_frequency
#define bzm_driver_state_init test_driver_state_init
#define bzm_driver_set_max_baud test_driver_set_max_baud
#define bzm_driver_submit_job test_driver_submit_job
#define bzm_driver_clear_work test_driver_clear_work
#define bzm_driver_job_frequency_ms test_driver_job_frequency_ms
#define bzm_driver_process_result test_driver_process_work
#define bzm_driver_read_temperature test_driver_read_temperature
#define bzm_driver_hashrate_counter_snapshot test_driver_hashrate_snapshot
#define bzm_driver_has_io_fault test_driver_has_io_fault
#define bzm_driver_work_replacement_snapshot test_driver_replacement_snapshot
#define bzm_driver_get_telemetry_snapshot test_driver_telemetry_snapshot
#define bzm_driver_step_frequency_domains test_driver_step_frequency_domains
#define bzm_driver_start test_driver_start
#define bzm_driver_get_state test_driver_get_state
#define bzm_driver_hold_reset test_driver_hold_reset
#define bzm_driver_set_dispatch_authorizer test_driver_dispatch_authorizer
#define bzm_driver_set_operation_authorizer test_driver_operation_authorizer
#define bzm_driver_poll test_driver_poll
#define bzm_serial_read_result test_driver_read_result
#define bzm_nonce_hash test_driver_hash
#define bzm_serial_poll test_driver_serial_poll
#define bzm_serial_get_telemetry_snapshot test_driver_copy_telemetry

#define bzm_control_get_safety_status test_driver_control_status
#define bzm_control_safe_off test_driver_safe_off
#define bzm_driver_service test_driver_service
#define bzm_bringup_live_frequency_domains_step test_driver_clock_step
#define bzm_control_set_asic_reset test_driver_reset
#define bzm_control_pulse_asic_reset test_driver_pulse
#define bzm_port_serial_prepare_session test_driver_serial_prepare
#define bzm_port_serial_wait_tx_done test_driver_wait_tx
#define bzm_bringup_start test_driver_chip_start
void *test_driver_calloc(size_t count, size_t size, bool large);
#define bzm_calloc test_driver_calloc
#include "../../bzm/bzm_driver.c"
#include "unity.h"

static bool authorized, fail_write, cancel_write, fail_flush, retire_during_hash;
static double result_difficulty;
static bzm_work_t last_work;
static bzm_raw_result_t raw_result;
static bzm_device_t test_state;
static bool reported_fault;
static void report_fault(void *context, const char *reason) { (void)context; (void)reason; reported_fault = true; }
static unsigned startup_failure, startup_reset_calls, startup_chip_calls, startup_nonce_writes;
static bool startup_reset_asserted;
static unsigned telemetry_polls, clock_steps;
static bool fail_clock, fresh_on_copy;
static bool telemetry_missing;
static TaskHandle_t shutdown_result_task;
void *test_driver_calloc(size_t count, size_t size, bool large) { (void)count; (void)size; (void)large; return NULL; }

bzm_serial_poll_result_t test_driver_serial_poll(bzm_serial_transport_t *transport, uint16_t timeout_ms)
{
    (void)timeout_ms;
    vTaskDelay(pdMS_TO_TICKS(1));
    unsigned index = telemetry_polls++ % BZM_BRINGUP_ASIC_COUNT;
    if (telemetry_missing && index == BZM_BRINGUP_ASIC_COUNT - 1)
        return (bzm_serial_poll_result_t){.status = BZM_OK};
    transport->io.telemetry.samples[index] = (bzm_telemetry_sample_t){
        .asic_id = bzm_asic_wire_ids[index], .received = true,
        .timestamp_us = (uint64_t)esp_timer_get_time(),
    };
    return (bzm_serial_poll_result_t){.status = BZM_OK, .bytes_received = 10, .frames_emitted = 1};
}

esp_err_t test_driver_reset(bool released)
{
    ++startup_reset_calls;
    if (startup_failure == 2) return ESP_FAIL;
    startup_reset_asserted = !released;
    if (!released && shutdown_result_task != NULL) xTaskNotifyGive(shutdown_result_task);
    return ESP_OK;
}
esp_err_t test_driver_pulse(void)
{
    startup_reset_asserted = false;
    telemetry_polls = 0;
    telemetry_missing = false;
    return startup_failure == 4 ? ESP_FAIL : ESP_OK;
}
esp_err_t test_driver_serial_prepare(int baud)
{
    TEST_ASSERT_EQUAL_INT(BZM_BAUD_RATE, baud);
    return startup_failure == 3 ? ESP_FAIL : ESP_OK;
}
bzm_status_t test_driver_wait_tx(uint32_t timeout_ms)
{
    TEST_ASSERT_EQUAL_UINT32(100, timeout_ms);
    return BZM_OK;
}
static int test_startup_nonce_write(void *context, const uint8_t *data, size_t size)
{
    (void)context;
    TEST_ASSERT_TRUE(DRIVER_BRINGUP.running);
    TEST_ASSERT_FALSE(bzm_dispatch_gate_is_authorized(&DRIVER_DISPATCH_GATE));
    TEST_ASSERT_EQUAL_UINT(20, size);
    TEST_ASSERT_EQUAL_HEX8(0x20, data[2] & 0xf0);
    TEST_ASSERT_TRUE(data[6] == BZM_REG_START_NONCE || data[6] == BZM_REG_END_NONCE);
    ++startup_nonce_writes;
    return (int)size;
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
    ((bzm_serial_transport_t *)context)->ops.write = test_startup_nonce_write;
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
bool test_driver_hash(const asic_job_t *job, uint32_t nonce, uint32_t version, uint8_t hash[32])
{
    (void)job; (void)nonce; (void)version;
    if (!isfinite(result_difficulty) || result_difficulty <= 0) return false;
    if (retire_during_hash) {
        retire_during_hash = false;
        TEST_ASSERT_TRUE(bzm_driver_clear_work(&test_state));
    }
    diff_to_target(result_difficulty, hash);
    return true;
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
                                   .nonce_offset = BZM_NONCE_GAP};
    bzm_transport_ops_t ops = {.write_work = write_work, .flush = flush};
    TEST_ASSERT_TRUE(bzm_reactor_init(&REACTOR, &BZM_STATE->job_store, &config, &ops, NULL));
    INITIALIZED = DRIVER_TRANSPORT_READY = DRIVER_BRINGUP.running = true;
    bzm_driver_set_dispatch_authorizer(allow_dispatch, NULL);
    authorized = true;
    fail_write = cancel_write = fail_flush = retire_during_hash = false;
    startup_failure = startup_reset_calls = startup_chip_calls = startup_nonce_writes = 0;
    startup_reset_asserted = false;
    clock_steps = 0;
    reported_fault = false;
    fail_clock = fresh_on_copy = false;
    memset(&test_state, 0, sizeof(test_state));
    test_state.asic_count = 4;
    test_state.core_count = 236;
    test_state.report_fault = report_fault;
    test_state.initialized = true;
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
                        .version_mask = 0x6000};
}

typedef struct {
    atomic_bool shared_initialized, completed;
    atomic_uint receive_calls;
    bool reset_held;
} shutdown_schedule_t;

static void shutdown_result_worker(void *context)
{
    shutdown_schedule_t *schedule = context;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    while (atomic_load(&schedule->shared_initialized)) {
        bzm_driver_process_result(&test_state);
        atomic_fetch_add(&schedule->receive_calls, 1);
    }
    vTaskSuspend(NULL);
}

static void shutdown_power_worker(void *context)
{
    shutdown_schedule_t *schedule = context;
    schedule->reset_held = bzm_driver_hold_reset();
    atomic_store(&schedule->shared_initialized, false);
    atomic_store(&schedule->completed, true);
    vTaskSuspend(NULL);
}

TEST_CASE("BZM stopped result worker lets lower priority shutdown complete", "[asic][bzm][driver][shutdown]")
{
    fixture_init();
    shutdown_schedule_t schedule;
    atomic_init(&schedule.shared_initialized, true);
    atomic_init(&schedule.completed, false);
    atomic_init(&schedule.receive_calls, 0);
    schedule.reset_held = false;
    UBaseType_t observer_priority = uxTaskPriorityGet(NULL);
    vTaskPrioritySet(NULL, configMAX_PRIORITIES - 1);
    BaseType_t core = xPortGetCoreID();
    TaskHandle_t power_task = NULL;
    BaseType_t receiver_created = xTaskCreatePinnedToCore(shutdown_result_worker,
        "stop result", 4096, &schedule, 15, &shutdown_result_task, core);
    BaseType_t power_created = receiver_created == pdPASS ?
        xTaskCreatePinnedToCore(shutdown_power_worker, "stop power", 4096,
            &schedule, 10, &power_task, core) : pdFAIL;
    vTaskDelay(pdMS_TO_TICKS(100));
    bool completed_in_time = atomic_load(&schedule.completed);
    unsigned receive_calls = atomic_load(&schedule.receive_calls);
    // Release a starving receiver before cleanup so even the negative control terminates.
    atomic_store(&schedule.shared_initialized, false);
    vTaskDelay(pdMS_TO_TICKS(20));
    if (shutdown_result_task != NULL) vTaskDelete(shutdown_result_task);
    shutdown_result_task = NULL;
    if (power_task != NULL) vTaskDelete(power_task);
    vTaskPrioritySet(NULL, observer_priority);
    bool reset_held = schedule.reset_held;
    fixture_destroy();
    printf("BZM shutdown: completed=%u result calls=%u reset=%u\n",
           completed_in_time, receive_calls, reset_held);
    TEST_ASSERT_EQUAL(pdPASS, receiver_created);
    TEST_ASSERT_EQUAL(pdPASS, power_created);
    TEST_ASSERT_GREATER_THAN_UINT32(0, receive_calls);
    TEST_ASSERT_TRUE(reset_held);
    TEST_ASSERT_TRUE_MESSAGE(completed_in_time, "stopped result polling starved lower-priority shutdown");
}

TEST_CASE("BZM telemetry acquisition collects separate ASIC slots within its window", "[asic][bzm][driver][startup]")
{
    fixture_init();
    TEST_ASSERT_TRUE(bzm_serial_transport_init(&TRANSPORT, &bzm_platform_serial_ops, NULL));
    bzm_driver_set_operation_authorizer(allow_dispatch, NULL);
    DRIVER_IO_OK = true;
    bzm_telemetry_store_t snapshot;
    TEST_ASSERT_TRUE(driver_telemetry_snapshot(&TRANSPORT, &snapshot, 100));
    TEST_ASSERT_EQUAL_UINT32(BZM_BRINGUP_ASIC_COUNT, telemetry_polls);
    for (unsigned i = 0; i < BZM_BRINGUP_ASIC_COUNT; ++i) TEST_ASSERT_TRUE(snapshot.samples[i].received);
    fixture_destroy();
}

TEST_CASE("BZM telemetry acquisition remains bounded when an ASIC does not report", "[asic][bzm][driver][startup]")
{
    fixture_init();
    TEST_ASSERT_TRUE(bzm_serial_transport_init(&TRANSPORT, &bzm_platform_serial_ops, NULL));
    bzm_driver_set_operation_authorizer(allow_dispatch, NULL);
    DRIVER_IO_OK = true;
    telemetry_missing = true;
    bzm_telemetry_store_t snapshot;
    int64_t started_us = esp_timer_get_time();
    TEST_ASSERT_TRUE(driver_telemetry_snapshot(&TRANSPORT, &snapshot, 20));
    TEST_ASSERT_GREATER_THAN_UINT32(1, telemetry_polls);
    TEST_ASSERT_FALSE(snapshot.samples[BZM_BRINGUP_ASIC_COUNT - 1].received);
    TEST_ASSERT_TRUE(esp_timer_get_time() - started_us < 100000);
    fixture_destroy();
}

TEST_CASE("BZM clean jobs restart a complete unpaced replacement rotation", "[asic][bzm][driver]")
{
    fixture_init();
    asic_job_t job = template();
    TEST_ASSERT_TRUE(bzm_driver_clear_work(&test_state));
    for (size_t i = 0; i < BZM_ENGINES_PER_ASIC - 1; ++i) TEST_ASSERT_TRUE(driver_send_work(&test_state, &job));
    TEST_ASSERT_TRUE(bzm_driver_clear_work(&test_state));
    uint32_t generation, completed;
    bool pending;
    for (size_t i = 0; i < BZM_ENGINES_PER_ASIC - 1; ++i) {
        TEST_ASSERT_TRUE(driver_send_work(&test_state, &job));
        TEST_ASSERT_EQUAL_DOUBLE(0.0, bzm_driver_job_frequency_ms(&test_state));
    }
    TEST_ASSERT_TRUE(driver_send_work(&test_state, &job));
    TEST_ASSERT_TRUE(bzm_driver_work_replacement_snapshot(&generation, &completed, &pending));
    TEST_ASSERT_FALSE(pending);
    TEST_ASSERT_EQUAL_UINT32(generation, completed);
    TEST_ASSERT_TRUE(bzm_driver_job_frequency_ms(&test_state) > 0);
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
        if (i % 25 == 0) TEST_ASSERT_TRUE(bzm_driver_clear_work(&test_state));
        TEST_ASSERT_TRUE(bzm_driver_work_replacement_snapshot(&generation, &completed, &pending));
        TEST_ASSERT_TRUE(pending);
        TEST_ASSERT_TRUE(driver_send_work(&test_state, &job));
    }
    TEST_ASSERT_TRUE(bzm_driver_work_replacement_snapshot(&generation, &completed, &pending));
    TEST_ASSERT_FALSE(pending);
    TEST_ASSERT_EQUAL_UINT32(1, generation);
    TEST_ASSERT_EQUAL_UINT32(generation, completed);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, bzm_driver_job_frequency_ms(&test_state));
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
        TEST_ASSERT_FALSE(driver_send_work(&test_state, &job));
        TEST_ASSERT_EQUAL(failure != 1, bzm_driver_has_io_fault());
        fixture_destroy();
    }
}

TEST_CASE("BZM difficulty filtering rejects nonfinite results and credits only valid hashes", "[asic][bzm][driver]")
{
    fixture_init();
    asic_job_t job = template();
    TEST_ASSERT_TRUE(driver_send_work(&test_state, &job));
    raw_result = (bzm_raw_result_t){.asic_id = BZM_FIRST_ASIC_ID, .engine_id = last_work.engine_id,
                                   .status = 8, .time = 16, .sequence_id = 0};
    const double cases[] = {NAN, INFINITY, -INFINITY, 0.0, 15.99, 16.0, 32.0};
    uint32_t counters[BZM_MAX_ASIC_COUNT] = {0};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        result_difficulty = cases[i];
        ++raw_result.nonce;
        const bzm_result_t *result = bzm_driver_process_result(&test_state);
        TEST_ASSERT_EQUAL(i >= 5, result != NULL);
    }
    TEST_ASSERT_TRUE(bzm_driver_hashrate_counter_snapshot(&test_state, counters, BZM_MAX_ASIC_COUNT));
    TEST_ASSERT_EQUAL_UINT32(32, counters[0]);
    TEST_ASSERT_EQUAL_UINT32(0, counters[1]);
    // A duplicate valid result must not earn a second hashrate credit.
    TEST_ASSERT_NULL(bzm_driver_process_result(&test_state));
    TEST_ASSERT_TRUE(bzm_driver_hashrate_counter_snapshot(&test_state, counters, BZM_MAX_ASIC_COUNT));
    TEST_ASSERT_EQUAL_UINT32(32, counters[0]);
    fixture_destroy();
}

TEST_CASE("BZM drops work retired during software nonce validation", "[asic][bzm][driver]")
{
    fixture_init();
    asic_job_t job = template();
    TEST_ASSERT_TRUE(driver_send_work(&test_state, &job));
    raw_result = (bzm_raw_result_t){.asic_id = BZM_FIRST_ASIC_ID, .engine_id = last_work.engine_id,
        .status = 8, .time = 16, .sequence_id = 0, .nonce = 123};
    result_difficulty = 32;
    retire_during_hash = true;
    TEST_ASSERT_NULL(bzm_driver_process_result(&test_state));
    TEST_ASSERT_FALSE(retire_during_hash);
    uint32_t counters[BZM_MAX_ASIC_COUNT];
    TEST_ASSERT_TRUE(bzm_driver_hashrate_counter_snapshot(&test_state, counters, BZM_MAX_ASIC_COUNT));
    TEST_ASSERT_EQUAL_UINT32(0, counters[0]);
    fixture_destroy();
}

TEST_CASE("BZM common jobs preserve version masks across updates and delayed results", "[asic][bzm][driver][version]")
{
    static const struct {
        uint32_t version, mask, versions[BZM_VERSION_VARIANTS];
    } cases[] = {
        {0x20000004, 0x00006000, {0x20000004, 0x20002004, 0x20004004, 0x20006004}},
        {0x3000a004, 0x00000000, {0x3000a004, 0x3000a004, 0x3000a004, 0x3000a004}},
        {0x3000a004, 0x1000a000, {0x3000a004, 0x20000004, 0x20002004, 0x20008004}},
        {0x20002004, 0x00002000, {0x20002004, 0x20000004, 0x20002004, 0x20000004}},
    };
    bzm_work_t sent[sizeof(cases) / sizeof(cases[0])];
    fixture_init();
    result_difficulty = 32;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        asic_job_t job = template();
        job.version = cases[i].version;
        job.version_mask = cases[i].mask;
        job.ntime += i;
        bzm_driver_submit_job(&test_state, &job);
        TEST_ASSERT_FALSE(bzm_driver_has_io_fault());
        sent[i] = last_work;
        TEST_ASSERT_EQUAL_HEX32_ARRAY(cases[i].versions, sent[i].versions, BZM_VERSION_VARIANTS);
        memset(&job, 0xa5, sizeof(job));
    }

    // Deliver newer and older work after all mask changes, with an ntime roll.
    for (size_t remaining = sizeof(cases) / sizeof(cases[0]); remaining > 0; --remaining) {
        size_t i = remaining - 1;
        for (unsigned variant = 0; variant < BZM_VERSION_VARIANTS; ++variant) {
            raw_result = (bzm_raw_result_t){
                .asic_id = BZM_FIRST_ASIC_ID, .engine_id = sent[i].engine_id,
                .status = 8, .time = 15, .nonce = 100 + variant,
                .sequence_id = (sent[i].logical_sequence << 2) | variant,
            };
            const bzm_result_t *result = bzm_driver_process_result(&test_state);
            TEST_ASSERT_NOT_NULL(result);
            TEST_ASSERT_EQUAL_HEX32(cases[i].version, result->job.version);
            TEST_ASSERT_EQUAL_HEX32(cases[i].mask, result->job.version_mask);
            TEST_ASSERT_EQUAL_HEX32(cases[i].versions[variant], result->final_version);
            TEST_ASSERT_EQUAL_HEX32(0, (result->final_version ^ result->job.version) & ~cases[i].mask);
            TEST_ASSERT_EQUAL_UINT32(1234 + i, result->job.ntime);
            TEST_ASSERT_EQUAL_UINT32(1235 + i, result->final_ntime);
        }
    }
    fixture_destroy();
}

TEST_CASE("BZM startup closes dispatch and attempts reset after transport or chip failure", "[asic][bzm][driver][startup]")
{
    for (unsigned failure = 1; failure <= 5; ++failure) {
        fixture_init();
        startup_failure = failure;
        authorized = failure != 1;
        bzm_driver_set_operation_authorizer(allow_dispatch, NULL);
        bzm_bringup_report_t report;
        bzm_bringup_telemetry_policy_t policy = {0};
        TEST_ASSERT_EQUAL(BZM_BRINGUP_BAD, bzm_driver_start(&test_state, &policy, &report));
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
    bzm_driver_set_operation_authorizer(allow_dispatch, NULL);
    bzm_bringup_report_t report;
    bzm_bringup_telemetry_policy_t policy = {0};
    TEST_ASSERT_EQUAL(BZM_BRINGUP_GOOD, bzm_driver_start(&test_state, &policy, &report));
    TEST_ASSERT_TRUE(INITIALIZED);
    TEST_ASSERT_TRUE(DRIVER_TRANSPORT_READY);
    TEST_ASSERT_TRUE(DRIVER_BRINGUP.running);
    TEST_ASSERT_FALSE(startup_reset_asserted);
    TEST_ASSERT_EQUAL_UINT32(1, startup_chip_calls);
    TEST_ASSERT_EQUAL_UINT8(BZM_RESULT_DIFFICULTY_POWER, test_state.difficulty_power);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bzm_asic_wire_ids, TRANSPORT.asic_ids, BZM_MAX_ASIC_COUNT);
    TEST_ASSERT_EQUAL_UINT16(BZM_ENGINE_ROWS, REFILL_REMAINING);
    TEST_ASSERT_EQUAL_UINT16(BZM_ENGINE_ROWS, BZM_STATE->work_replacement_remaining);
    TEST_ASSERT_TRUE(REACTOR.config.multicast_rows);
    TEST_ASSERT_EQUAL_UINT32(2 * 4 * 236, startup_nonce_writes);
    TEST_ASSERT_FALSE(bzm_dispatch_gate_is_authorized(&DRIVER_DISPATCH_GATE));
    bzm_driver_set_dispatch_authorizer(allow_dispatch, NULL);
    TEST_ASSERT_TRUE(bzm_dispatch_gate_is_authorized(&DRIVER_DISPATCH_GATE));
    fixture_destroy();
}

TEST_CASE("BZM startup cancellation stays latched across subsequent I/O", "[asic][bzm][driver][startup]")
{
    fixture_init();
    bzm_driver_set_operation_authorizer(allow_dispatch, NULL);
    DRIVER_IO_OK = true;
    TEST_ASSERT_TRUE(driver_operation_check());
    authorized = false;
    driver_delay_ms(NULL, 1000);
    TEST_ASSERT_FALSE(DRIVER_IO_OK);
    authorized = true;
    TEST_ASSERT_FALSE(driver_operation_check());
    TEST_ASSERT_FALSE(driver_write_u32(&TRANSPORT, 0x10, 0, 0, 0));
    fixture_destroy();
}

esp_err_t test_driver_control_status(bzm_control_status_t *status)
{
    *status = (bzm_control_status_t){.valid = true, .core_enabled = true, .five_volt_enabled = true};
    return ESP_OK;
}
esp_err_t test_driver_safe_off(bzm_control_status_t *status)
{
    *status = (bzm_control_status_t){.valid = true};
    startup_reset_asserted = true;
    return ESP_OK;
}

bzm_bringup_outcome_t test_driver_clock_step(bzm_bringup_state_t *state, const bzm_bringup_ops_t *ops, void *ctx,
    const float targets[BZM_BRINGUP_ASIC_COUNT][BZM_BRINGUP_PLL_COUNT], bool jump, bzm_bringup_report_t *report)
{
    (void)ops; (void)ctx;
    ++clock_steps;
    TEST_ASSERT_FALSE(jump);
    TEST_ASSERT_TRUE(fabsf(targets[0][0] - state->clock_mhz) <= 25.001f);
    *report = (bzm_bringup_report_t){.reason = fail_clock ? BZM_BRINGUP_REASON_IO : BZM_BRINGUP_REASON_NONE};
    if (fail_clock) return BZM_BRINGUP_BAD;
    for (unsigned chip = 0; chip < BZM_BRINGUP_ASIC_COUNT; ++chip)
        for (unsigned pll = 0; pll < BZM_BRINGUP_PLL_COUNT; ++pll)
            TEST_ASSERT_EQUAL_FLOAT(targets[0][0], targets[chip][pll]);
    state->clock_mhz = targets[0][0];
    return BZM_BRINGUP_GOOD;
}

TEST_CASE("BZM portable init starts at 800 MHz and can resume without owning a task", "[asic][bzm][driver][startup]")
{
    fixture_init();
    TEST_ASSERT_EQUAL_UINT8(4, bzm_driver_init(&test_state));
    TEST_ASSERT_EQUAL_FLOAT(800, test_state.actual_frequency_mhz);
    TEST_ASSERT_TRUE(bzm_dispatch_gate_is_authorized(&DRIVER_DISPATCH_GATE));
    TEST_ASSERT_TRUE(bzm_driver_hold_reset());
    TEST_ASSERT_FALSE(INITIALIZED);
    TEST_ASSERT_EQUAL_UINT8(4, bzm_driver_init(&test_state));
    bzm_driver_service(&test_state);
    TEST_ASSERT_FALSE(reported_fault);
    fixture_destroy();
}

TEST_CASE("BZM shared frequency entry ramps all PLLs and replaces work", "[asic][bzm][driver]")
{
    fixture_init();
    TEST_ASSERT_EQUAL_UINT8(4, bzm_driver_init(&test_state));
    test_state.target_frequency_mhz = 856.25f;
    bzm_driver_set_frequency(&test_state);
    TEST_ASSERT_EQUAL_FLOAT(856.25f, test_state.actual_frequency_mhz);
    TEST_ASSERT_EQUAL_UINT32(3, clock_steps);
    TEST_ASSERT_EQUAL_UINT16(BZM_ENGINE_ROWS, BZM_STATE->work_replacement_remaining);
    TEST_ASSERT_TRUE(REACTOR.config.multicast_rows);
    TEST_ASSERT_EQUAL_UINT32(2 * 4 * 236, startup_nonce_writes);
    test_state.target_frequency_mhz = 800;
    bzm_driver_set_frequency(&test_state);
    TEST_ASSERT_EQUAL_FLOAT(800, test_state.actual_frequency_mhz);
    TEST_ASSERT_FALSE(reported_fault);
    fixture_destroy();
}

TEST_CASE("BZM failed frequency change stops at the first failed step", "[asic][bzm][driver]")
{
    fixture_init();
    TEST_ASSERT_EQUAL_UINT8(4, bzm_driver_init(&test_state));
    fail_clock = true;
    test_state.target_frequency_mhz = 1000;
    bzm_driver_set_frequency(&test_state);
    TEST_ASSERT_EQUAL_UINT32(1, clock_steps);
    TEST_ASSERT_TRUE(reported_fault);
    TEST_ASSERT_TRUE(startup_reset_asserted);
    TEST_ASSERT_FALSE(INITIALIZED);
    fixture_destroy();
}

#undef bzm_serial_get_telemetry_snapshot
bool bzm_serial_get_telemetry_snapshot(bzm_serial_transport_t *transport, bzm_telemetry_store_t *snapshot);
bool test_driver_copy_telemetry(bzm_serial_transport_t *transport, bzm_telemetry_store_t *snapshot)
{
    bool available = bzm_serial_get_telemetry_snapshot(transport, snapshot);
    if (available && fresh_on_copy) {
        // Model telemetry arriving while the temperature reader waits for the driver lock.
        vTaskDelay(pdMS_TO_TICKS(1));
        for (size_t i = 0; i < BZM_MAX_ASIC_COUNT; ++i) {
            snapshot->samples[i] = (bzm_telemetry_sample_t){
                .asic_id = bzm_asic_wire_ids[i], .received = true, .thermal_valid = true,
                .temperature_c = 45.0f + i, .timestamp_us = (uint64_t)esp_timer_get_time(),
            };
        }
    }
    return available;
}

TEST_CASE("BZM temperature accepts telemetry received while waiting for the driver lock", "[asic][bzm][driver]")
{
    fixture_init();
    TEST_ASSERT_TRUE(bzm_serial_transport_init(&TRANSPORT, &bzm_platform_serial_ops, NULL));
    TRANSPORT.asic_count = BZM_MAX_ASIC_COUNT;
    fresh_on_copy = true;
    TEST_ASSERT_EQUAL_FLOAT(48.0f, bzm_driver_read_temperature(&test_state));
    fixture_destroy();
}
