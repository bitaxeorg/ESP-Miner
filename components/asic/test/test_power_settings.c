#include "unity.h"
#include "global_state.h"
#include <math.h>
#include <string.h>

#define POWER_MANAGEMENT_apply_settings test_apply_power_settings
#define VCORE_set_voltage power_settings_voltage
#define ASIC_set_frequency power_settings_frequency
#define ASIC_set_nonce_space power_settings_nonce_space
#define ASIC_set_difficulty power_settings_difficulty
#include "../../../main/power/power_settings.c"
#undef POWER_MANAGEMENT_apply_settings
#undef VCORE_set_voltage
#undef ASIC_set_frequency
#undef ASIC_set_nonce_space
#undef ASIC_set_difficulty

static char operations[16];
static unsigned operation_count;
static bool voltage_failure;
static bool frequency_failure;
static float voltage_command;

static void record_operation(char operation)
{
    TEST_ASSERT_LESS_THAN(sizeof(operations) - 1, operation_count);
    operations[operation_count++] = operation;
    operations[operation_count] = '\0';
}

esp_err_t power_settings_voltage(GlobalState *state, float voltage)
{
    (void)state;
    record_operation('V');
    voltage_command = voltage;
    return voltage_failure ? ESP_FAIL : ESP_OK;
}

bool power_settings_frequency(GlobalState *state)
{
    record_operation('F');
    if (!frequency_failure) {
        state->POWER_MANAGEMENT_MODULE.actual_frequency = state->POWER_MANAGEMENT_MODULE.frequency_value;
    }
    return !frequency_failure;
}

void power_settings_nonce_space(GlobalState *state)
{
    (void)state;
    record_operation('N');
}

void power_settings_difficulty(GlobalState *state, uint8_t difficulty)
{
    (void)state;
    (void)difficulty;
    record_operation('D');
}

static GlobalState initial_state(void)
{
    memset(operations, 0, sizeof(operations));
    operation_count = 0;
    voltage_failure = false;
    frequency_failure = false;
    GlobalState state = {0};
    state.DEVICE_CONFIG.family.asic_count = 1;
    state.DEVICE_CONFIG.family.asic.small_core_count = 2040;
    return state;
}

TEST_CASE("Voltage increases precede the frequency ramp", "[power_settings]")
{
    GlobalState state = initial_state();
    uint16_t voltage = 1150;
    float frequency = 600;
    TEST_ASSERT_TRUE(test_apply_power_settings(&state, 1250, 750, &voltage, &frequency));
    TEST_ASSERT_EQUAL_STRING("VFND", operations);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 1.25, voltage_command);
    TEST_ASSERT_EQUAL_UINT16(1250, voltage);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 750, frequency);
}

TEST_CASE("Voltage reductions follow the frequency ramp", "[power_settings]")
{
    GlobalState state = initial_state();
    uint16_t voltage = 1250;
    float frequency = 750;
    TEST_ASSERT_TRUE(test_apply_power_settings(&state, 1150, 600, &voltage, &frequency));
    TEST_ASSERT_EQUAL_STRING("FNDV", operations);
    TEST_ASSERT_EQUAL_UINT16(1150, voltage);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 600, frequency);
}

TEST_CASE("A failed voltage increase prevents ramping and is retried", "[power_settings]")
{
    GlobalState state = initial_state();
    uint16_t voltage = 1150;
    float frequency = 600;
    voltage_failure = true;
    TEST_ASSERT_FALSE(test_apply_power_settings(&state, 1250, 750, &voltage, &frequency));
    TEST_ASSERT_EQUAL_STRING("V", operations);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 600, frequency);
    TEST_ASSERT_EQUAL_UINT16(0, voltage);
    voltage_failure = false;
    operation_count = 0;
    TEST_ASSERT_TRUE(test_apply_power_settings(&state, 1250, 750, &voltage, &frequency));
    TEST_ASSERT_EQUAL_STRING("VFND", operations);
}

TEST_CASE("A failed frequency ramp prevents reducing voltage", "[power_settings]")
{
    GlobalState state = initial_state();
    uint16_t voltage = 1250;
    float frequency = 750;
    frequency_failure = true;
    TEST_ASSERT_FALSE(test_apply_power_settings(&state, 1150, 600, &voltage, &frequency));
    TEST_ASSERT_EQUAL_STRING("FN", operations);
    TEST_ASSERT_EQUAL_UINT16(1250, voltage);
    TEST_ASSERT_TRUE(isnan(frequency));
    frequency_failure = false;
    operation_count = 0;
    TEST_ASSERT_TRUE(test_apply_power_settings(&state, 1150, 750, &voltage, &frequency));
    TEST_ASSERT_EQUAL_STRING("FNDV", operations);
}

TEST_CASE("A failed voltage reduction retries voltage without repeating the ramp", "[power_settings]")
{
    GlobalState state = initial_state();
    uint16_t voltage = 1250;
    float frequency = 750;
    voltage_failure = true;
    TEST_ASSERT_FALSE(test_apply_power_settings(&state, 1150, 600, &voltage, &frequency));
    TEST_ASSERT_EQUAL_STRING("FNDV", operations);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 600, frequency);
    voltage_failure = false;
    operation_count = 0;
    TEST_ASSERT_TRUE(test_apply_power_settings(&state, 1150, 600, &voltage, &frequency));
    TEST_ASSERT_EQUAL_STRING("V", operations);
}

TEST_CASE("Unchanged settings produce no writes", "[power_settings]")
{
    GlobalState state = initial_state();
    uint16_t voltage = 1150;
    float frequency = 600;
    TEST_ASSERT_TRUE(test_apply_power_settings(&state, 1150, 600, &voltage, &frequency));
    TEST_ASSERT_EQUAL_STRING("", operations);
}

TEST_CASE("A lower request after a failed voltage increase downclocks first", "[power_settings]")
{
    GlobalState state = initial_state();
    state.POWER_MANAGEMENT_MODULE.actual_frequency = 750;
    uint16_t voltage = 1250;
    float frequency = 750;
    voltage_failure = true;
    TEST_ASSERT_FALSE(test_apply_power_settings(&state, 1350, 850, &voltage, &frequency));
    TEST_ASSERT_EQUAL_UINT16(0, voltage);
    voltage_failure = false;
    operation_count = 0;
    TEST_ASSERT_TRUE(test_apply_power_settings(&state, 1150, 600, &voltage, &frequency));
    TEST_ASSERT_EQUAL_STRING("FNDV", operations);
}

TEST_CASE("An uncertain rail is reestablished before increasing frequency", "[power_settings]")
{
    GlobalState state = initial_state();
    state.POWER_MANAGEMENT_MODULE.actual_frequency = 750;
    uint16_t voltage = 1250;
    float frequency = 750;
    voltage_failure = true;
    TEST_ASSERT_FALSE(test_apply_power_settings(&state, 1150, 600, &voltage, &frequency));
    TEST_ASSERT_EQUAL_UINT16(0, voltage);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 600, state.POWER_MANAGEMENT_MODULE.actual_frequency);
    voltage_failure = false;
    operation_count = 0;
    TEST_ASSERT_TRUE(test_apply_power_settings(&state, 1200, 700, &voltage, &frequency));
    TEST_ASSERT_EQUAL_STRING("VFND", operations);
}
