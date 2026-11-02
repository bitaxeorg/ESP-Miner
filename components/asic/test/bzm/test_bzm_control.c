#include <string.h>
#include "driver/gpio.h"
#include "unity.h"
#include "device_pins.h"

esp_err_t control_test_reset_pin(gpio_num_t pin);
int control_test_power_good(gpio_num_t pin);
#define gpio_reset_pin control_test_reset_pin
#define gpio_get_level control_test_power_good
#define BZM_control_init control_test_init
#define BZM_control_get_safety_status control_test_status
#define BZM_control_clear_safety_fault control_test_clear
#define BZM_control_safe_off control_test_safe_off
#define BZM_control_set_5v_enabled control_test_5v
#define BZM_control_set_asic_reset control_test_reset
#define BZM_control_set_fan_percent control_test_fan
#define BZM_control_get_fan_rpm control_test_rpm
#define BZM_control_set_core_enabled control_test_core
#define BZM_control_reset_bridge control_test_bridge_reset
#define bzm_bridge_reset control_test_swd_reset
#define bzm_gpio_init control_test_gpio_init
#define bzm_gpio_get_safety_status control_test_gpio_status
#include "components/asic/bzm/port/esp/control.c"

static unsigned init_calls;
static esp_err_t init_result;
static gpio_num_t reset_pins[4];
static unsigned pin_reset_calls, fail_pin_reset;
static bzm_control_status_t control_status;
static unsigned swd_reset_calls;
static esp_err_t swd_reset_result;
static bool trip_after_reset;
static bool power_good;
int control_test_power_good(gpio_num_t pin)
{
    TEST_ASSERT_EQUAL(GPIO_NUM_11, pin);
    return power_good;
}
esp_err_t control_test_swd_reset(gpio_num_t clock, gpio_num_t data)
{
    TEST_ASSERT_EQUAL(GPIO_NUM_14, clock);
    TEST_ASSERT_EQUAL(GPIO_NUM_15, data);
    ++swd_reset_calls;
    if (trip_after_reset) control_status.fault = BZM_CONTROL_FAULT_ASIC_TRIP;
    return swd_reset_result;
}
esp_err_t control_test_reset_pin(gpio_num_t pin)
{
    TEST_ASSERT_LESS_THAN_UINT32(4, pin_reset_calls);
    reset_pins[pin_reset_calls++] = pin;
    return pin_reset_calls == fail_pin_reset ? ESP_FAIL : ESP_OK;
}
esp_err_t control_test_gpio_init(const BzmPins *pins)
{
    TEST_ASSERT_EQUAL_PTR(&BONANZA_1004_CONTROL_PINS, pins);
    ++init_calls;
    return init_result;
}
esp_err_t control_test_gpio_status(bzm_control_status_t *status)
{
    *status = control_status;
    return ESP_OK;
}

static void control_fixture(void)
{
    initialized = false;
    init_calls = pin_reset_calls = fail_pin_reset = 0;
    init_result = ESP_OK;
    control_status = (bzm_control_status_t){.valid = true, .asic_reset_asserted = true};
    swd_reset_calls = 0;
    swd_reset_result = ESP_OK;
    trip_after_reset = false;
    power_good = true;
    memset(reset_pins, 0, sizeof(reset_pins));
}

TEST_CASE("Bridge reset requires ASIC reset and preserves safety faults", "[asic][bonanza][control]")
{
    control_fixture();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, control_test_bridge_reset());
    TEST_ASSERT_EQUAL(ESP_OK, control_test_init("1004", &BONANZA_1004_CONTROL_PINS));
    control_status.asic_reset_asserted = false;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, control_test_bridge_reset());
    control_status.asic_reset_asserted = true;
    control_status.valid = false;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, control_test_bridge_reset());
    control_status.valid = true;
    control_status.fault = BZM_CONTROL_FAULT_ASIC_TRIP;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, control_test_bridge_reset());
    control_status.fault = BZM_CONTROL_FAULT_NONE;
    control_status.trip_input_asserted = true;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, control_test_bridge_reset());
    TEST_ASSERT_EQUAL_UINT32(0, swd_reset_calls);
    control_status.trip_input_asserted = false;
    control_status.core_enabled = true;
    power_good = false;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, control_test_bridge_reset());
    TEST_ASSERT_EQUAL_UINT32(0, swd_reset_calls);
    power_good = true;
    TEST_ASSERT_EQUAL(ESP_OK, control_test_bridge_reset());
    TEST_ASSERT_EQUAL_UINT32(1, swd_reset_calls);
    trip_after_reset = true;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, control_test_bridge_reset());
    control_status.fault = BZM_CONTROL_FAULT_NONE;
    trip_after_reset = false;
    swd_reset_result = ESP_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, control_test_bridge_reset());
}

TEST_CASE("Bonanza control accepts only 1004 before touching hardware", "[asic][bonanza][control]")
{
    control_fixture();
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, control_test_init(NULL, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, control_test_init("1002", NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, control_test_init("1005", NULL));
    TEST_ASSERT_EQUAL_UINT32(0, init_calls);
    TEST_ASSERT_EQUAL_UINT32(0, pin_reset_calls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, control_test_core(true));
    TEST_ASSERT_EQUAL(ESP_OK, control_test_init("1004", &BONANZA_1004_CONTROL_PINS));
    TEST_ASSERT_EQUAL(ESP_OK, control_test_init("1004", NULL));
    TEST_ASSERT_EQUAL_UINT32(1, init_calls);
    TEST_ASSERT_EQUAL_UINT32(2, pin_reset_calls);
    TEST_ASSERT_EQUAL(GPIO_NUM_43, reset_pins[0]);
    TEST_ASSERT_EQUAL(GPIO_NUM_44, reset_pins[1]);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, control_test_init("1002", NULL));
    bzm_control_status_t status;
    TEST_ASSERT_EQUAL(ESP_OK, control_test_status(&status));
    TEST_ASSERT_TRUE(status.valid);
}

TEST_CASE("Bonanza control requires configured pins before its first initialization", "[asic][bonanza][control]")
{
    control_fixture();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, control_test_init("1004", NULL));
    TEST_ASSERT_EQUAL_UINT32(0, init_calls);
    TEST_ASSERT_EQUAL_UINT32(0, pin_reset_calls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, control_test_core(true));
}

TEST_CASE("Bonanza control initialization failure leaves status unusable and permits retry", "[asic][bonanza][control]")
{
    control_fixture();
    init_result = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, control_test_init("1004", &BONANZA_1004_CONTROL_PINS));
    bzm_control_status_t status = {.valid = true};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, control_test_status(&status));
    TEST_ASSERT_FALSE(status.valid);
    TEST_ASSERT_EQUAL(BZM_CONTROL_FAULT_STATUS_INVALID, status.fault);
    init_result = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, control_test_init("1004", &BONANZA_1004_CONTROL_PINS));
    TEST_ASSERT_EQUAL_UINT32(2, init_calls);
}

TEST_CASE("Bonanza control fails before board initialization if UART0 pin release fails", "[asic][bonanza][control]")
{
    for (unsigned failed = 1; failed <= 2; ++failed) {
        control_fixture();
        fail_pin_reset = failed;
        TEST_ASSERT_EQUAL(ESP_FAIL, control_test_init("1004", &BONANZA_1004_CONTROL_PINS));
        TEST_ASSERT_EQUAL_UINT32(failed, pin_reset_calls);
        TEST_ASSERT_EQUAL_UINT32(0, init_calls);
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, control_test_core(true));
        fail_pin_reset = 0;
        TEST_ASSERT_EQUAL(ESP_OK, control_test_init("1004", &BONANZA_1004_CONTROL_PINS));
        TEST_ASSERT_EQUAL_UINT32(failed + 2, pin_reset_calls);
        TEST_ASSERT_EQUAL(GPIO_NUM_43, reset_pins[failed]);
        TEST_ASSERT_EQUAL(GPIO_NUM_44, reset_pins[failed + 1]);
        TEST_ASSERT_EQUAL_UINT32(1, init_calls);
    }
}

TEST_CASE("Bonanza fault clear requires all local outputs safe and trip low", "[asic][bonanza][control]")
{
    bzm_control_status_t status = {
        .valid = true, .state = BZM_CONTROL_STATE_FAULT_LATCHED, .fault = BZM_CONTROL_FAULT_ASIC_TRIP,
        .asic_reset_asserted = true, .fan_full = true, .fan_percent = 100,
    };
    TEST_ASSERT_TRUE(bzm_control_status_allows_fault_clear(&status));
    status.core_enabled = true;
    TEST_ASSERT_FALSE(bzm_control_status_allows_fault_clear(&status));
    status.core_enabled = false;
    status.five_volt_enabled = true;
    TEST_ASSERT_FALSE(bzm_control_status_allows_fault_clear(&status));
    status.five_volt_enabled = false;
    status.asic_reset_asserted = false;
    TEST_ASSERT_FALSE(bzm_control_status_allows_fault_clear(&status));
    status.asic_reset_asserted = true;
    status.fan_full = false;
    TEST_ASSERT_FALSE(bzm_control_status_allows_fault_clear(&status));
    status.fan_full = true;
    status.fan_percent = 99;
    TEST_ASSERT_FALSE(bzm_control_status_allows_fault_clear(&status));
    status.fan_percent = 100;
    status.trip_input_asserted = true;
    TEST_ASSERT_FALSE(bzm_control_status_allows_fault_clear(&status));
    status.trip_input_asserted = false;
    status.valid = false;
    TEST_ASSERT_FALSE(bzm_control_status_allows_fault_clear(&status));
    TEST_ASSERT_FALSE(bzm_control_status_allows_fault_clear(NULL));
}
