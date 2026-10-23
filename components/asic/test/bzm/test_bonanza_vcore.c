#include "bzm_test_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "unity.h"

static void vcore_delay(TickType_t ticks) { (void)ticks; }
static int vcore_gpio_level(gpio_num_t pin);
#define vTaskDelay vcore_delay
#define gpio_get_level vcore_gpio_level
#define BONANZA_VCORE_init test_vcore_init
#define BONANZA_VCORE_set_voltage test_vcore_set_voltage
#define BZM_control_init vcore_control_init
#define BZM_control_set_5v_enabled vcore_set_5v
#define BZM_control_set_core_enabled vcore_set_core
#define BZM_control_safe_off vcore_safe_off
#define BZM_control_clear_safety_fault vcore_clear_fault
#define BZM_hold_reset vcore_hold_reset
#define BZM_get_state vcore_asic_state
#define TPS546_init vcore_tps_init
#define TPS546_set_vout vcore_tps_set
#define TPS546_snapshot_status vcore_tps_status
#define TPS546_log_snapshot vcore_tps_log
#include "../../../../main/power/bonanza_vcore.c"

static bool core_enabled, five_volt_enabled, asic_running, fail_pgood;
static bool fail_reset, fail_5v_off, fail_vout_off, fail_core_off, fail_fault_clear, fail_runtime_write;
static float rail_voltage;
static unsigned rail_starts, voltage_writes, fault_clears;
static char operations[64];
static unsigned operation_count;
static GlobalState state;

static void record_operation(char operation)
{
    TEST_ASSERT_LESS_THAN_UINT32(sizeof(operations) - 1, operation_count);
    operations[operation_count++] = operation;
    operations[operation_count] = '\0';
}

esp_err_t vcore_control_init(const char *version) { (void)version; return ESP_OK; }
esp_err_t vcore_set_5v(bool enabled)
{
    record_operation(enabled ? 'F' : '5');
    if (enabled) { TEST_ASSERT_TRUE(core_enabled); TEST_ASSERT_EQUAL_FLOAT(2.8f, rail_voltage); }
    if (!enabled && fail_5v_off) return ESP_ERR_TIMEOUT;
    five_volt_enabled = enabled;
    return ESP_OK;
}
esp_err_t vcore_set_core(bool enabled)
{
    record_operation(enabled ? 'E' : 'D');
    if (!enabled && fail_core_off) return ESP_ERR_TIMEOUT;
    core_enabled = enabled;
    if (enabled) ++rail_starts;
    return ESP_OK;
}
esp_err_t vcore_safe_off(bzm_control_status_t *status)
{
    record_operation('S');
    core_enabled = five_volt_enabled = asic_running = false;
    *status = (bzm_control_status_t){.valid = true};
    return ESP_OK;
}
esp_err_t vcore_clear_fault(bzm_control_status_t *status)
{
    (void)status;
    record_operation('C');
    ++fault_clears;
    return fail_fault_clear ? ESP_ERR_TIMEOUT : ESP_OK;
}
bool vcore_hold_reset(void)
{
    record_operation('R');
    if (fail_reset) return false;
    asic_running = false;
    return true;
}
bool vcore_asic_state(bzm_bringup_state_t *asic) { *asic = (bzm_bringup_state_t){.running = asic_running}; return true; }
static int vcore_gpio_level(gpio_num_t pin) { (void)pin; return !fail_pgood && core_enabled && rail_voltage > 0; }
esp_err_t vcore_tps_init(TPS546_CONFIG value) { (void)value; return ESP_OK; }
esp_err_t vcore_tps_set(float voltage)
{
    record_operation('V');
    ++voltage_writes;
    if ((voltage == 0 && fail_vout_off) || (voltage != 0 && fail_runtime_write)) return ESP_ERR_TIMEOUT;
    rail_voltage = voltage;
    return ESP_OK;
}
esp_err_t vcore_tps_status(TPS546_StatusSnapshot *snapshot)
{
    *snapshot = (TPS546_StatusSnapshot){.operation = 0x80, .vout_command = rail_voltage,
        .read_vout = rail_voltage, .read_vin = 12, .read_temp1 = 40};
    return ESP_OK;
}
void vcore_tps_log(const TPS546_StatusSnapshot *snapshot) { (void)snapshot; }
static void fixture(void)
{
    state = (GlobalState){0};
    state.DEVICE_CONFIG.family = FAMILY_BONANZA;
    state.DEVICE_CONFIG.TPS546 = true;
    rail_enabled = core_enabled = five_volt_enabled = asic_running = fail_pgood = false;
    fail_reset = fail_5v_off = fail_vout_off = fail_core_off = fail_fault_clear = fail_runtime_write = false;
    rail_voltage = 0;
    rail_starts = voltage_writes = fault_clears = operation_count = 0;
    TEST_ASSERT_EQUAL(ESP_OK, test_vcore_init(&state, TPS546_CONFIG_BONANZA_1004));
    operation_count = 0;
    operations[0] = '\0';
}

TEST_CASE("Bonanza shared voltage setter preserves startup rail then applies live settings", "[asic][bonanza][vcore]")
{
    fixture();
    TEST_ASSERT_EQUAL(ESP_OK, test_vcore_set_voltage(&state, 2.5f));
    TEST_ASSERT_EQUAL_FLOAT(2.8f, rail_voltage);
    TEST_ASSERT_TRUE(five_volt_enabled);
    TEST_ASSERT_EQUAL_STRING("SC5EVF", operations);
    TEST_ASSERT_EQUAL(ESP_OK, test_vcore_set_voltage(&state, 3.0f));
    TEST_ASSERT_EQUAL_FLOAT(2.8f, rail_voltage);
    TEST_ASSERT_EQUAL_UINT32(1, rail_starts);
    TEST_ASSERT_EQUAL_UINT32(1, voltage_writes);
    TEST_ASSERT_EQUAL_UINT32(1, fault_clears);
    asic_running = true;
    TEST_ASSERT_EQUAL(ESP_OK, test_vcore_set_voltage(&state, 2.5f));
    TEST_ASSERT_EQUAL_FLOAT(2.5f, rail_voltage);
    TEST_ASSERT_EQUAL(ESP_OK, test_vcore_set_voltage(&state, 3.0f));
    TEST_ASSERT_EQUAL_FLOAT(3.0f, rail_voltage);
    TEST_ASSERT_EQUAL_UINT32(1, rail_starts);
    TEST_ASSERT_EQUAL_UINT32(3, voltage_writes);
    TEST_ASSERT_EQUAL_UINT32(1, fault_clears);
}

TEST_CASE("Bonanza pause powers down both rails and resume repeats electrical startup", "[asic][bonanza][vcore]")
{
    fixture();
    TEST_ASSERT_EQUAL(ESP_OK, test_vcore_set_voltage(&state, 2.9f));
    asic_running = true;
    TEST_ASSERT_EQUAL(ESP_OK, test_vcore_set_voltage(&state, 0));
    TEST_ASSERT_FALSE(asic_running);
    TEST_ASSERT_FALSE(core_enabled);
    TEST_ASSERT_FALSE(five_volt_enabled);
    TEST_ASSERT_EQUAL_FLOAT(0, rail_voltage);
    TEST_ASSERT_EQUAL(ESP_OK, test_vcore_set_voltage(&state, 2.9f));
    TEST_ASSERT_EQUAL_UINT32(2, rail_starts);
    TEST_ASSERT_EQUAL_FLOAT(2.8f, rail_voltage);
}

TEST_CASE("Bonanza invalid voltage and failed PGOOD cannot leave the rail enabled", "[asic][bonanza][vcore]")
{
    fixture();
    TEST_ASSERT_NOT_EQUAL(ESP_OK, test_vcore_set_voltage(&state, 1.0f));
    TEST_ASSERT_FALSE(core_enabled);
    fail_pgood = true;
    TEST_ASSERT_NOT_EQUAL(ESP_OK, test_vcore_set_voltage(&state, 2.8f));
    TEST_ASSERT_FALSE(core_enabled);
    TEST_ASSERT_FALSE(five_volt_enabled);
    TEST_ASSERT_FALSE(rail_enabled);
    TEST_ASSERT_EQUAL_FLOAT(0, rail_voltage);
}

TEST_CASE("Bonanza shutdown attempts every step and reports reset or rail errors", "[asic][bonanza][vcore]")
{
    for (unsigned failure = 0; failure < 5; ++failure) {
        fixture();
        TEST_ASSERT_EQUAL(ESP_OK, test_vcore_set_voltage(&state, 2.9f));
        asic_running = true;
        fail_reset = failure == 0 || failure == 4;
        fail_5v_off = failure == 1 || failure == 4;
        fail_vout_off = failure == 2;
        fail_core_off = failure == 3;
        operation_count = 0;
        TEST_ASSERT_EQUAL(failure == 0 ? ESP_FAIL : ESP_ERR_TIMEOUT, test_vcore_set_voltage(&state, 0));
        TEST_ASSERT_EQUAL_STRING("R5VD", operations);
        TEST_ASSERT_FALSE(rail_enabled);
        TEST_ASSERT_EQUAL_UINT32(1, fault_clears);
    }
}

TEST_CASE("Bonanza runtime voltage failures hold reset and disable both rails", "[asic][bonanza][vcore]")
{
    for (unsigned failure = 0; failure < 2; ++failure) {
        fixture();
        TEST_ASSERT_EQUAL(ESP_OK, test_vcore_set_voltage(&state, 2.9f));
        asic_running = true;
        fail_runtime_write = failure == 0;
        fail_pgood = failure == 1;
        TEST_ASSERT_EQUAL(failure == 0 ? ESP_ERR_TIMEOUT : ESP_FAIL, test_vcore_set_voltage(&state, 2.9f));
        TEST_ASSERT_FALSE(asic_running);
        TEST_ASSERT_FALSE(core_enabled);
        TEST_ASSERT_FALSE(five_volt_enabled);
        TEST_ASSERT_FALSE(rail_enabled);
        TEST_ASSERT_EQUAL_FLOAT(0, rail_voltage);
        TEST_ASSERT_TRUE(state.SYSTEM_MODULE.hardware_fault);
        TEST_ASSERT_EQUAL_STRING("BZM rail configuration failed", state.SYSTEM_MODULE.hardware_fault_msg);
        TEST_ASSERT_EQUAL_UINT32(1, fault_clears);
    }
}

TEST_CASE("Bonanza startup cannot enable the rail when fault clearing fails", "[asic][bonanza][vcore]")
{
    fixture();
    fail_fault_clear = true;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, test_vcore_set_voltage(&state, 2.9f));
    TEST_ASSERT_EQUAL_STRING("SCR5VD", operations);
    TEST_ASSERT_EQUAL_UINT32(0, rail_starts);
    TEST_ASSERT_FALSE(core_enabled);
    TEST_ASSERT_FALSE(five_volt_enabled);
    TEST_ASSERT_FALSE(rail_enabled);
    TEST_ASSERT_TRUE(state.SYSTEM_MODULE.hardware_fault);
}
