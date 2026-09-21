#include "bzm_board_test_bindings.h"
#include "bzm/bridge_protocol.h"
#include "bzm/telemetry_internal.h"
#include "../../../main/power/bzm_board_power.c"
#include "unity.h"

static struct {
    uint64_t now_us;
    bzm_bridge_safety_status_t bridge;
    bool rail_on, stuck_rail, snapshot_failed, stop_requested, io_fault;
    bool replacement_pending;
    uint32_t replacement_generation;
    float clock_mhz, voltage_v;
    char fail_operation, cancel_operation;
    char calls[256];
    size_t call_count;
    unsigned arms, clears;
} hardware;
static GlobalState board_state;
static bzm_runtime_state_t board_runtime;

static bool record_operation(char operation)
{
    TEST_ASSERT_LESS_THAN_UINT32(sizeof(hardware.calls) - 1, hardware.call_count);
    hardware.calls[hardware.call_count++] = operation;
    if (hardware.cancel_operation == operation) hardware.stop_requested = true;
    return hardware.fail_operation != operation;
}

static void update_bridge(void)
{
    bzm_bridge_safety_status_t *b = &hardware.bridge;
    b->evidence = BZM_BRIDGE_SAFETY_EVIDENCE_TRIP_CLEAR | BZM_BRIDGE_SAFETY_EVIDENCE_FAULT_CLEAR;
    bool controlled = b->state == BZM_BRIDGE_SAFETY_STATE_CONTROLLED;
    b->runtime_verdict = controlled ? BZM_BRIDGE_SAFETY_RUNTIME_GOOD_CONTROLLED : BZM_BRIDGE_SAFETY_RUNTIME_GOOD_SAFE_OFF;
    b->lease_remaining_ms = controlled ? 2000 : 0;
    if (controlled) b->evidence |= BZM_BRIDGE_SAFETY_EVIDENCE_LEASE_VALID;
    if (!b->five_volt_enabled && b->asic_reset_asserted && b->fan_full)
        b->evidence |= BZM_BRIDGE_SAFETY_EVIDENCE_OUTPUTS_SAFE;
}

static void fixture_init(void)
{
    memset(&hardware, 0, sizeof(hardware));
    memset(&board_state, 0, sizeof(board_state));
    memset(&board_runtime, 0, sizeof(board_runtime));
    hardware.now_us = 1000000;
    hardware.replacement_pending = true;
    hardware.replacement_generation = 1;
    hardware.clock_mhz = 800.0f;
    hardware.voltage_v = 2.8f;
    hardware.bridge = (bzm_bridge_safety_status_t){
        .valid = true, .schema_version = BZM_BRIDGE_SAFETY_STATUS_SCHEMA_VERSION,
        .stage = BZM_BRIDGE_SAFETY_STAGE_TRIP_LATCH,
        .state = BZM_BRIDGE_SAFETY_STATE_SAFE_OFF,
        .production_verdict = BZM_BRIDGE_SAFETY_PRODUCTION_BAD_STAGE_DISABLED,
        .capabilities = BZM_BRIDGE_SAFETY_CAP_5V_CONTROL | BZM_BRIDGE_SAFETY_CAP_ASIC_RESET_CONTROL |
                        BZM_BRIDGE_SAFETY_CAP_FAN_FORCE_FULL | BZM_BRIDGE_SAFETY_CAP_TRIP_INPUT_SAMPLED |
                        BZM_BRIDGE_SAFETY_CAP_FAN_CONTROLLED_SPEED,
        .asic_reset_asserted = true, .fan_full = true, .fan_percent = 100,
    };
    update_bridge();
    board_state.DEVICE_CONFIG.family = FAMILY_BONANZA;
    board_state.SELF_TEST_MODULE.is_active = true;
    RUNTIME_STATE = &board_runtime;
    RUNTIME.global_state = &board_state;
    RUNTIME.initialized = true;
    RUNTIME.safe_off = true;
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_init(&RUNTIME.lock, NULL));
    atomic_init(&RUNTIME.pause_requested, false);
    atomic_init(&RUNTIME.dispatch_enabled, false);
    atomic_init(&RUNTIME.dispatch_deadline_ms, 0);
    atomic_init(&RUNTIME.execution_deadline_ms, 0);
    atomic_init(&RUNTIME.execution_cancelled, false);
}

static void fixture_destroy(void)
{
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_destroy(&RUNTIME.lock));
    RUNTIME_STATE = NULL;
}

int64_t board_test_time(void) { return hardware.now_us; }
void board_test_delay(TickType_t ticks) { hardware.now_us += (uint64_t)ticks * portTICK_PERIOD_MS * 1000; }
bool board_test_stop_requested(void) { return hardware.stop_requested; }
bool board_test_io_fault(void) { return hardware.io_fault; }
float board_test_saved_float(NvsConfigKey key) { (void)key; return 800.0f; }
uint16_t board_test_saved_u16(NvsConfigKey key) { (void)key; return 2800; }
void board_test_dispatch_authorizer(bzm_dispatch_authorizer_t authorize, void *context) { (void)authorize; (void)context; }
bool board_test_hold_reset(void) { return true; }
bool board_test_replacement(uint32_t *generation, uint32_t *completed, bool *pending)
{
    *generation = hardware.replacement_generation;
    *pending = hardware.replacement_pending;
    *completed = *generation - (*pending ? 1 : 0);
    return true;
}
bool board_test_driver_state(bzm_bringup_state_t *state)
{
    *state = (bzm_bringup_state_t){.running = true, .clock_mhz = hardware.clock_mhz};
    for (size_t asic = 0; asic < BZM_BRINGUP_ASIC_COUNT; ++asic)
        for (size_t pll = 0; pll < BZM_BRINGUP_PLL_COUNT; ++pll)
            state->domain_clock_mhz[asic][pll] = hardware.clock_mhz;
    return true;
}
bzm_bringup_outcome_t board_test_frequency(
    const float target[BZM_BRINGUP_ASIC_COUNT][BZM_BRINGUP_PLL_COUNT],
    bool initial_jump, bzm_bringup_report_t *report, float *actual)
{
    (void)initial_jump;
    (void)report;
    if (!record_operation('L')) return BZM_BRINGUP_BAD;
    hardware.clock_mhz = *actual = target[0][0];
    hardware.replacement_pending = true;
    hardware.replacement_generation++;
    return BZM_BRINGUP_GOOD;
}

esp_err_t board_test_bridge_info(bzm_bridge_info_t *info)
{
    *info = (bzm_bridge_info_t){.schema_version = BZM_BRIDGE_INFO_SCHEMA_VERSION,
                              .protocol_major = BZM_BRIDGE_PROTOCOL_MAJOR,
                              .protocol_minor = BZM_BRIDGE_PROTOCOL_MINOR};
    return ESP_OK;
}
esp_err_t board_test_bridge_status(bzm_bridge_safety_status_t *status)
{ update_bridge(); *status = hardware.bridge; return ESP_OK; }
esp_err_t board_test_arm(bzm_bridge_safety_status_t *status)
{
    hardware.arms++;
    if (!record_operation('A')) return ESP_FAIL;
    hardware.bridge.state = BZM_BRIDGE_SAFETY_STATE_CONTROLLED;
    return board_test_bridge_status(status);
}
esp_err_t board_test_disarm(bzm_bridge_safety_status_t *status)
{
    hardware.bridge.state = BZM_BRIDGE_SAFETY_STATE_SAFE_OFF;
    return board_test_bridge_status(status);
}
esp_err_t board_test_clear_fault(bzm_bridge_safety_status_t *status)
{
    hardware.clears++;
    hardware.bridge.fault = BZM_BRIDGE_SAFETY_FAULT_NONE;
    return board_test_bridge_status(status);
}
esp_err_t board_test_heartbeat(bzm_bridge_safety_status_t *status) { return board_test_bridge_status(status); }
esp_err_t board_test_rpm(uint16_t *rpm) { *rpm = 2200; return ESP_OK; }
esp_err_t board_test_five_volt(bool enabled)
{
    if (!record_operation(enabled ? 'V' : 'v')) return ESP_FAIL;
    hardware.bridge.five_volt_enabled = enabled;
    return ESP_OK;
}
esp_err_t board_test_reset(bool released)
{
    if (!record_operation(released ? 'R' : 'r')) return ESP_FAIL;
    hardware.bridge.asic_reset_asserted = !released;
    return ESP_OK;
}
esp_err_t board_test_fan(DeviceConfig *config, float percent)
{
    (void)config;
    TEST_ASSERT_EQUAL_FLOAT(1.0f, percent);
    return record_operation('F') ? ESP_OK : ESP_FAIL;
}
esp_err_t board_test_rail(GlobalState *state, bool enabled)
{
    (void)state;
    if (!record_operation(enabled ? 'P' : 'p')) return ESP_FAIL;
    hardware.rail_on = enabled;
    return ESP_OK;
}
esp_err_t board_test_voltage(GlobalState *state, float voltage)
{
    (void)state;
    if (!record_operation(voltage > hardware.voltage_v ? 'U' : 'u')) return ESP_FAIL;
    hardware.voltage_v = voltage;
    return ESP_OK;
}
esp_err_t board_test_power_snapshot(BONANZA_TPS546_StatusSnapshot *snapshot, bool *pgood)
{
    if (hardware.snapshot_failed) return ESP_FAIL;
    bool on = hardware.rail_on || hardware.stuck_rail;
    *pgood = on;
    *snapshot = (BONANZA_TPS546_StatusSnapshot){
        .operation = on ? OPERATION_ON : 0,
        .vout_command = hardware.voltage_v, .vout_command_matches_active_config = true,
        .read_vin = 12.0f, .read_vout = on ? hardware.voltage_v : 0.0f,
        .read_iout = on ? 10.0f : 0.0f, .read_temp1 = 50,
    };
    return ESP_OK;
}
esp_err_t board_test_check_protection(char *detail, size_t capacity)
{
    snprintf(detail, capacity, "simulated readback");
    return record_operation('Q') ? ESP_OK : ESP_FAIL;
}

bzm_bringup_outcome_t board_test_start(GlobalState *state, const bzm_bringup_telemetry_policy_t *policy,
                                       bzm_bringup_report_t *report)
{
    (void)state;
    (void)policy;
    hardware.io_fault = false;
    hardware.bridge.asic_reset_asserted = false;
    bool ok = record_operation('M');
    *report = (bzm_bringup_report_t){.reason = ok ? BZM_BRINGUP_REASON_NONE : BZM_BRINGUP_REASON_IO};
    return ok ? BZM_BRINGUP_GOOD : BZM_BRINGUP_BAD;
}

bool board_test_telemetry(bzm_telemetry_store_t *store)
{
    bzm_telemetry_store_init(store);
    for (size_t i = 0; i < BZM_MAX_ASIC_COUNT; ++i) {
        store->samples[i] = (bzm_telemetry_sample_t){
            .asic_id = bzm_asic_wire_ids[i], .timestamp_us = hardware.now_us,
            .received = true, .valid = true, .temperature_c = 50.0f,
            .thermal_enabled = true, .thermal_validity = true, .thermal_valid = true,
            .ch0_mv = 400.0f, .ch1_mv = 410.0f, .ch2_mv = 1.0f,
            .voltage_enabled = true, .voltage_valid = true, .pll_locked = true,
        };
    }
    return true;
}

TEST_CASE("Bonanza production startup opens dispatch without a nonce proof", "[asic][bzm][board]")
{
    fixture_init();
    TEST_ASSERT_EQUAL(BONANZA_POWER_START_OK, BZM_board_start(NULL));
    TEST_ASSERT_TRUE(board_state.ASIC_initalized);
    TEST_ASSERT_TRUE(runtime_dispatch_authorizer(&RUNTIME));
    TEST_ASSERT_TRUE(RUNTIME.replacement_pending);
    TEST_ASSERT_EQUAL_UINT32(1, hardware.arms);
    TEST_ASSERT_NOT_NULL(strstr(hardware.calls, "M"));
    TEST_ASSERT_TRUE(BZM_board_stop(NULL));
    TEST_ASSERT_FALSE(board_state.ASIC_initalized);
    TEST_ASSERT_FALSE(runtime_dispatch_authorizer(&RUNTIME));
    TEST_ASSERT_FALSE(hardware.rail_on);
    TEST_ASSERT_TRUE(RUNTIME.safe_off);
    fixture_destroy();
}

TEST_CASE("Bonanza rolls back every failed startup operation", "[asic][bzm][board]")
{
    const char failures[] = "APQVM";
    for (size_t i = 0; i < sizeof(failures) - 1; ++i) {
        fixture_init();
        hardware.fail_operation = failures[i];
        TEST_ASSERT_EQUAL(BONANZA_POWER_START_FAILED, BZM_board_start(NULL));
        TEST_ASSERT_FALSE(runtime_dispatch_authorizer(&RUNTIME));
        TEST_ASSERT_FALSE(board_state.ASIC_initalized);
        TEST_ASSERT_FALSE(hardware.rail_on);
        TEST_ASSERT_FALSE(hardware.bridge.five_volt_enabled);
        TEST_ASSERT_TRUE(hardware.bridge.asic_reset_asserted);
        TEST_ASSERT_TRUE(RUNTIME.safe_off);
        TEST_ASSERT_TRUE(RUNTIME.fault_latched);
        TEST_ASSERT_NOT_NULL(strchr(hardware.calls, failures[i]));
        fixture_destroy();
    }
}

TEST_CASE("Bonanza cancellation during startup shuts down without latching a fault", "[asic][bzm][board]")
{
    fixture_init();
    hardware.cancel_operation = 'M';
    TEST_ASSERT_EQUAL(BONANZA_POWER_START_FAILED, BZM_board_start(NULL));
    TEST_ASSERT_TRUE(atomic_load(&RUNTIME.execution_cancelled));
    TEST_ASSERT_FALSE(RUNTIME.fault_latched);
    TEST_ASSERT_TRUE(RUNTIME.safe_off);
    fixture_destroy();
}

TEST_CASE("Bonanza shutdown attempts all outputs and requires fresh electrical evidence", "[asic][bzm][board]")
{
    for (unsigned failure = 0; failure < 3; ++failure) {
        fixture_init();
        TEST_ASSERT_EQUAL(BONANZA_POWER_START_OK, BZM_board_start(NULL));
        hardware.call_count = 0;
        memset(hardware.calls, 0, sizeof(hardware.calls));
        hardware.fail_operation = failure == 0 ? 'r' : 0;
        hardware.stuck_rail = failure == 1;
        hardware.snapshot_failed = failure == 2;
        TEST_ASSERT_FALSE(BZM_board_stop(NULL));
        TEST_ASSERT_EQUAL_STRING("rvFp", hardware.calls);
        TEST_ASSERT_FALSE(RUNTIME.safe_off);
        TEST_ASSERT_TRUE(RUNTIME.fault_latched);
        TEST_ASSERT_FALSE(runtime_dispatch_authorizer(&RUNTIME));
        fixture_destroy();
    }
}

TEST_CASE("Bonanza maintenance rechecks rail discharge before clearing a retained fault", "[asic][bzm][board]")
{
    fixture_init();
    hardware.bridge.fault = BZM_BRIDGE_SAFETY_FAULT_LEASE_EXPIRED;
    hardware.stuck_rail = true;
    TEST_ASSERT_FALSE(BZM_board_maintenance(NULL, BONANZA_POWER_OWNER_OTA, true));
    TEST_ASSERT_EQUAL_UINT32(0, hardware.clears);
    hardware.stuck_rail = false;
    TEST_ASSERT_TRUE(BZM_board_maintenance(NULL, BONANZA_POWER_OWNER_OTA, true));
    TEST_ASSERT_EQUAL_UINT32(1, hardware.clears);
    TEST_ASSERT_FALSE(RUNTIME.fault_latched);
    TEST_ASSERT_FALSE(runtime_dispatch_authorizer(&RUNTIME));
    fixture_destroy();
}

TEST_CASE("Bonanza watchdog expiry cannot be renewed by a late owner sample", "[asic][bzm][board]")
{
    fixture_init();
    TEST_ASSERT_EQUAL(BONANZA_POWER_START_OK, BZM_board_start(NULL));
    hardware.now_us = RUNTIME.watchdog_deadline_ms * 1000;
    TEST_ASSERT_FALSE(runtime_dispatch_authorizer(&RUNTIME));
    TEST_ASSERT_EQUAL(BONANZA_POWER_HEALTH_FAULT, BZM_board_sample().health);
    hardware.now_us += 1000;
    TEST_ASSERT_EQUAL(BONANZA_POWER_HEALTH_FAULT, BZM_board_sample().health);
    TEST_ASSERT_FALSE(atomic_load(&RUNTIME.dispatch_enabled));
    fixture_destroy();
}

TEST_CASE("Bonanza transport faults and pool loss revoke dispatch immediately", "[asic][bzm][board]")
{
    fixture_init();
    TEST_ASSERT_EQUAL(BONANZA_POWER_START_OK, BZM_board_start(NULL));
    board_state.SYSTEM_MODULE.pools_unavailable = true;
    TEST_ASSERT_FALSE(runtime_dispatch_authorizer(&RUNTIME));
    board_state.SYSTEM_MODULE.pools_unavailable = false;
    hardware.io_fault = true;
    TEST_ASSERT_FALSE(runtime_dispatch_authorizer(&RUNTIME));
    TEST_ASSERT_EQUAL(BZM_RUNTIME_HEALTH_BAD, sample_runtime_health_locked().status);
    TEST_ASSERT_EQUAL(BONANZA_POWER_HEALTH_FAULT, BZM_board_sample().health);
    TEST_ASSERT_EQUAL(BONANZA_POWER_START_FAILED, BZM_board_start(NULL));
    TEST_ASSERT_EQUAL(BZM_RUNTIME_HEALTH_BAD, RUNTIME.health.status);
    fixture_destroy();
}

TEST_CASE("Bonanza tuning waits for work replacement and orders voltage around clocks", "[asic][bzm][board]")
{
    fixture_init();
    TEST_ASSERT_EQUAL(BONANZA_POWER_START_OK, BZM_board_start(NULL));
    hardware.call_count = 0;
    memset(hardware.calls, 0, sizeof(hardware.calls));
    bonanza_power_target_t target = {.voltage_mv = 3000, .frequency_mhz = 1200};
    TEST_ASSERT_TRUE(BZM_board_apply(NULL, target));
    TEST_ASSERT_EQUAL_STRING("", hardware.calls);
    hardware.replacement_pending = false;
    TEST_ASSERT_TRUE(BZM_board_apply(NULL, target));
    TEST_ASSERT_EQUAL_STRING("U", hardware.calls);
    TEST_ASSERT_TRUE(BZM_board_apply(NULL, target));
    TEST_ASSERT_EQUAL_STRING("UL", hardware.calls);
    TEST_ASSERT_TRUE(BZM_board_apply(NULL, target));
    TEST_ASSERT_EQUAL_STRING("UL", hardware.calls);

    target = (bonanza_power_target_t){.voltage_mv = 2800, .frequency_mhz = 800};
    for (unsigned step = 0; step < 20 && hardware.voltage_v > 2.8f; ++step) {
        hardware.replacement_pending = false;
        TEST_ASSERT_TRUE(BZM_board_apply(NULL, target));
        if (hardware.clock_mhz > 800.0f) TEST_ASSERT_EQUAL_FLOAT(3.0f, hardware.voltage_v);
    }
    TEST_ASSERT_EQUAL_FLOAT(800.0f, hardware.clock_mhz);
    TEST_ASSERT_EQUAL_FLOAT(2.8f, hardware.voltage_v);
    TEST_ASSERT_EQUAL_CHAR('u', hardware.calls[hardware.call_count - 1]);
    fixture_destroy();
}
