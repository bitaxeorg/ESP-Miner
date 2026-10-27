#include "bzm/bzm_test_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "unity.h"
#include <string.h>

void shared_init_frequency(GlobalState *state);
static void shared_delay(TickType_t ticks);
static void shared_delete(TaskHandle_t task);
static esp_err_t shared_flush(uart_port_t port);
#define vTaskDelay shared_delay
#define vTaskDelete shared_delete
#define uart_flush shared_flush
#define nvs_config_get_u16 shared_get_u16
#define nvs_config_set_u16 shared_set_u16
#define nvs_config_get_float shared_get_float
#define nvs_config_set_float shared_set_float
#define nvs_config_get_bool shared_get_bool
#define nvs_config_set_bool shared_set_bool
#define VCORE_set_voltage shared_voltage
#define VCORE_get_voltage_mv shared_voltage_mv
#define VCORE_get_voltage_min_mv shared_min_voltage
#define VCORE_check_fault shared_check_fault
#define Power_get_input_voltage shared_input
#define Power_get_output shared_output
#define Power_get_vreg_temp shared_vr_temp
#define Thermal_get_chip_temp shared_chip_temp
#define Thermal_get_chip_temp2 shared_chip_temp2
#define ASIC_init shared_asic_init
#define ASIC_set_frequency shared_frequency
#define ASIC_get_min_frequency shared_min_frequency
#define ASIC_set_nonce_space shared_nonce_space
#define ASIC_set_max_baud shared_max_baud
#define asic_hold_reset_low shared_hold_reset
#define asic_reset shared_reset
#define difficulty_controller_update shared_difficulty
#define asic_initialize shared_initialize
#define SERIAL_init shared_serial_init
#define SERIAL_is_initialized shared_serial_ready
#define SERIAL_set_baud shared_serial_baud
#define SERIAL_clear_buffer shared_serial_clear
#define POWER_MANAGEMENT_init_frequency shared_init_frequency
#define POWER_MANAGEMENT_task shared_power_task
#define TAG power_tag
#include "../../../main/tasks/power_management_task.c"
#undef TAG
#define TAG init_tag
#include "../../../main/power/asic_init.c"
#undef TAG

static GlobalState state;
static float saved_frequency, applied_frequency, applied_voltage, chip_temp;
static uint16_t saved_voltage;
static float board_input_mv;
static unsigned chip_inits, clock_writes, serial_calls, resets, cooling_delays, polls;
static bool overheat, edit_settings, fail_init;

uint16_t shared_get_u16(NvsConfigKey key) { return key == NVS_CONFIG_ASIC_VOLTAGE ? saved_voltage : 100; }
void shared_set_u16(NvsConfigKey key, uint16_t value) { if (key == NVS_CONFIG_ASIC_VOLTAGE) saved_voltage = value; }
float shared_get_float(NvsConfigKey key) { (void)key; return saved_frequency; }
void shared_set_float(NvsConfigKey key, float value) { (void)key; saved_frequency = value; }
bool shared_get_bool(NvsConfigKey key) { return key == NVS_CONFIG_OVERHEAT_MODE && overheat; }
void shared_set_bool(NvsConfigKey key, bool value) { if (key == NVS_CONFIG_OVERHEAT_MODE) overheat = value; }
esp_err_t shared_voltage(GlobalState *s, float volts) { (void)s; applied_voltage = volts; return ESP_OK; }
int16_t shared_voltage_mv(GlobalState *s) { (void)s; return applied_voltage * 1000; }
int16_t shared_min_voltage(GlobalState *s) { return s->DEVICE_CONFIG.family.asic.id == BZM ? 2100 : 1000; }
esp_err_t shared_check_fault(GlobalState *s) { (void)s; return ESP_OK; }
float shared_input(GlobalState *s) { (void)s; return board_input_mv; }
void shared_output(GlobalState *s, float *power, float *current) { (void)s; *power = 40; *current = 3.3f; }
float shared_vr_temp(GlobalState *s) { (void)s; return 40; }
float shared_chip_temp(GlobalState *s) { (void)s; return chip_temp; }
float shared_chip_temp2(GlobalState *s) { (void)s; return -1; }
float shared_min_frequency(GlobalState *s) { return s->DEVICE_CONFIG.family.asic.id == BZM ? 800 : 50; }
void shared_frequency(GlobalState *s) { ++clock_writes; applied_frequency = s->POWER_MANAGEMENT_MODULE.frequency_value; }
void shared_nonce_space(GlobalState *s) { (void)s; }
void shared_difficulty(GlobalState *s) { (void)s; }
uint8_t shared_asic_init(GlobalState *s) { ++chip_inits; return fail_init ? 0 : s->DEVICE_CONFIG.family.asic_count; }
int shared_max_baud(GlobalState *s) { (void)s; return 1000000; }
esp_err_t shared_hold_reset(void) { ++resets; return ESP_OK; }
esp_err_t shared_reset(uint32_t low, uint32_t high) { (void)low; (void)high; ++resets; return ESP_OK; }
esp_err_t shared_serial_init(void) { ++serial_calls; return ESP_OK; }
bool shared_serial_ready(void) { return false; }
esp_err_t shared_serial_baud(int baud) { (void)baud; ++serial_calls; return ESP_OK; }
void shared_serial_clear(void) { ++serial_calls; }
static esp_err_t shared_flush(uart_port_t port) { (void)port; return ESP_OK; }
static void shared_delete(TaskHandle_t task) { (void)task; }
static void shared_delay(TickType_t ticks)
{
    if (ticks == pdMS_TO_TICKS(5000)) { ++cooling_delays; chip_temp = 30; }
    if (ticks == pdMS_TO_TICKS(100)) {
        ++polls;
        if (edit_settings && polls == 1) { saved_frequency = 900; saved_voltage = 2900; }
        else state.SELF_TEST_MODULE.is_finished = true;
    }
}
static void fixture(AsicConfig asic)
{
    memset(&state, 0, sizeof(state));
    state.DEVICE_CONFIG.family = FAMILY_BONANZA;
    state.DEVICE_CONFIG.family.asic = asic;
    state.ASIC_initalized = true;
    saved_frequency = asic.default_frequency_mhz;
    saved_voltage = asic.default_voltage_mv;
    applied_frequency = applied_voltage = 0;
    chip_temp = 40;
    board_input_mv = 12000;
    chip_inits = clock_writes = serial_calls = resets = cooling_delays = polls = 0;
    overheat = edit_settings = fail_init = false;
}

TEST_CASE("Shared power stops BZM at 800 MHz and restores saved settings on resume", "[asic][power][shared]")
{
    fixture(ASIC_BZM);
    saved_frequency = 1100; saved_voltage = 2950;
    shared_init_frequency(&state);
    TEST_ASSERT_EQUAL_FLOAT(800, state.POWER_MANAGEMENT_MODULE.actual_frequency);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1384.5333f, state.POWER_MANAGEMENT_MODULE.expected_hashrate);
    mining_stop(&state);
    TEST_ASSERT_EQUAL_FLOAT(800, applied_frequency);
    TEST_ASSERT_EQUAL_FLOAT(0, applied_voltage);
    TEST_ASSERT_FALSE(state.ASIC_initalized);
    TEST_ASSERT_EQUAL_UINT8(4, mining_start(&state));
    TEST_ASSERT_EQUAL_FLOAT(1100, applied_frequency);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.95f, applied_voltage);
    TEST_ASSERT_TRUE(state.ASIC_initalized);
    TEST_ASSERT_EQUAL_UINT32(0, serial_calls);
}

TEST_CASE("Shared power shuts down BZM after the driver reports a hardware fault", "[asic][power][shared]")
{
    fixture(ASIC_BZM);
    applied_voltage = 2.95f;
    state.SYSTEM_MODULE.hardware_fault = true;
    shared_power_task(&state);
    TEST_ASSERT_EQUAL_FLOAT(0, applied_voltage);
    TEST_ASSERT_EQUAL_FLOAT(800, applied_frequency);
    TEST_ASSERT_EQUAL_FLOAT(0, state.POWER_MANAGEMENT_MODULE.expected_hashrate);
    TEST_ASSERT_FALSE(state.ASIC_initalized);
    TEST_ASSERT_TRUE(state.SYSTEM_MODULE.hardware_fault);
    TEST_ASSERT_EQUAL_UINT32(1, resets);
    TEST_ASSERT_EQUAL_UINT32(1, clock_writes);
    TEST_ASSERT_EQUAL_UINT32(0, chip_inits);
}

TEST_CASE("Shared thermal recovery respects the BZM frequency and voltage floors", "[asic][power][shared]")
{
    fixture(ASIC_BZM);
    saved_frequency = 825; saved_voltage = 2150; chip_temp = 80;
    shared_power_task(&state);
    TEST_ASSERT_EQUAL_FLOAT(800, saved_frequency);
    TEST_ASSERT_EQUAL_UINT16(2100, saved_voltage);
    TEST_ASSERT_EQUAL_UINT32(6, cooling_delays);
    TEST_ASSERT_EQUAL_FLOAT(800, applied_frequency);
    TEST_ASSERT_TRUE(state.ASIC_initalized);
    TEST_ASSERT_FALSE(overheat);
}

TEST_CASE("Shared power applies live BZM settings through the common interfaces", "[asic][power][shared]")
{
    fixture(ASIC_BZM);
    edit_settings = true;
    shared_power_task(&state);
    TEST_ASSERT_EQUAL_FLOAT(900, applied_frequency);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.9f, applied_voltage);
    TEST_ASSERT_EQUAL_UINT32(1, clock_writes);
}

TEST_CASE("Shared power retains the Bitmain stop frequency and UART initialization", "[asic][power][shared]")
{
    fixture(ASIC_BM1370);
    shared_init_frequency(&state);
    TEST_ASSERT_EQUAL_FLOAT(50, state.POWER_MANAGEMENT_MODULE.actual_frequency);
    mining_stop(&state);
    TEST_ASSERT_EQUAL_FLOAT(50, applied_frequency);
    TEST_ASSERT_EQUAL_UINT8(4, mining_start(&state));
    TEST_ASSERT_GREATER_THAN_UINT32(0, serial_calls);
    TEST_ASSERT_TRUE(state.ASIC_initalized);
}

TEST_CASE("BZM initialization failure leaves power off and mining uninitialized", "[asic][power][shared]")
{
    fixture(ASIC_BZM);
    state.ASIC_initalized = false;
    fail_init = true;
    TEST_ASSERT_EQUAL_UINT8(0, shared_initialize(&state, ASIC_INIT_COLD_BOOT, 0));
    TEST_ASSERT_EQUAL_FLOAT(0, applied_voltage);
    TEST_ASSERT_FALSE(state.ASIC_initalized);
    TEST_ASSERT_EQUAL_UINT32(0, clock_writes);
}

TEST_CASE("Shared thermal guard ignores board input voltage when ASIC settings are at minimum", "[asic][power][shared]")
{
    const FamilyConfig families[] = {FAMILY_BONANZA, FAMILY_GAMMA};
    const float board_inputs[] = {5000, 12000};
    for (unsigned a = 0; a < sizeof(families) / sizeof(families[0]); ++a) {
        for (unsigned b = 0; b < sizeof(board_inputs) / sizeof(board_inputs[0]); ++b) {
            fixture(families[a].asic);
            state.DEVICE_CONFIG.family = families[a];
            saved_frequency = shared_min_frequency(&state);
            saved_voltage = shared_min_voltage(&state);
            applied_voltage = saved_voltage / 1000.0f;
            board_input_mv = board_inputs[b];
            chip_temp = 80;
            shared_power_task(&state);
            TEST_ASSERT_EQUAL_UINT32(0, cooling_delays);
            TEST_ASSERT_EQUAL_UINT32(0, resets);
            TEST_ASSERT_EQUAL_UINT32(0, chip_inits);
            TEST_ASSERT_EQUAL_UINT16(shared_min_voltage(&state), saved_voltage);
        }
    }
}

TEST_CASE("Shared thermal guard permits voltage reduction at minimum ASIC frequency", "[asic][power][shared]")
{
    const FamilyConfig families[] = {FAMILY_BONANZA, FAMILY_GAMMA};
    for (unsigned a = 0; a < sizeof(families) / sizeof(families[0]); ++a) {
        fixture(families[a].asic);
        state.DEVICE_CONFIG.family = families[a];
        saved_frequency = shared_min_frequency(&state);
        saved_voltage = shared_min_voltage(&state) + 50;
        applied_voltage = saved_voltage / 1000.0f;
        board_input_mv = 0; // Missing board-input telemetry must not mask the ASIC voltage.
        chip_temp = 80;
        shared_power_task(&state);
        TEST_ASSERT_EQUAL_UINT32(6, cooling_delays);
        TEST_ASSERT_GREATER_THAN_UINT32(0, resets);
        TEST_ASSERT_EQUAL_UINT32(1, chip_inits);
        TEST_ASSERT_EQUAL_UINT16(shared_min_voltage(&state), saved_voltage);
    }
}
