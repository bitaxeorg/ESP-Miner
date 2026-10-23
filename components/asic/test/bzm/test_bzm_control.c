#include <string.h>
#include "sdkconfig.h"
#include "unity.h"

/* Exercise the supported USB-console configuration without initializing GPIOs. */
#undef CONFIG_ESP_CONSOLE_UART_DEFAULT
#define CONFIG_ESP_CONSOLE_UART_DEFAULT 0
#define BZM_control_init control_test_init
#define BZM_control_get_safety_status control_test_status
#define BZM_control_clear_safety_fault control_test_clear
#define BZM_control_safe_off control_test_safe_off
#define BZM_control_set_5v_enabled control_test_5v
#define BZM_control_set_asic_reset control_test_reset
#define BZM_control_set_fan_percent control_test_fan
#define BZM_control_get_fan_rpm control_test_rpm
#define BZM_control_set_core_enabled control_test_core
#define bzm_gpio_init control_test_gpio_init
#define bzm_gpio_get_safety_status control_test_gpio_status
#include "../../bzm/port/esp/control.c"

static unsigned init_calls;
static esp_err_t init_result;
esp_err_t control_test_gpio_init(void) { ++init_calls; return init_result; }
esp_err_t control_test_gpio_status(bzm_control_status_t *status)
{
    *status = (bzm_control_status_t){.valid = true, .state = BZM_CONTROL_STATE_SAFE_OFF};
    return ESP_OK;
}

TEST_CASE("Bonanza control accepts only 1004 before touching hardware", "[asic][bonanza][control]")
{
    initialized = false;
    init_calls = 0;
    init_result = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, control_test_init(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, control_test_init("1002"));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, control_test_init("1005"));
    TEST_ASSERT_EQUAL_UINT32(0, init_calls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, control_test_core(true));
    TEST_ASSERT_EQUAL(ESP_OK, control_test_init("1004"));
    TEST_ASSERT_EQUAL(ESP_OK, control_test_init("1004"));
    TEST_ASSERT_EQUAL_UINT32(1, init_calls);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, control_test_init("1002"));
    bzm_control_status_t status;
    TEST_ASSERT_EQUAL(ESP_OK, control_test_status(&status));
    TEST_ASSERT_TRUE(status.valid);
}

TEST_CASE("Bonanza control initialization failure leaves status unusable and permits retry", "[asic][bonanza][control]")
{
    initialized = false;
    init_calls = 0;
    init_result = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, control_test_init("1004"));
    bzm_control_status_t status = {.valid = true};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, control_test_status(&status));
    TEST_ASSERT_FALSE(status.valid);
    TEST_ASSERT_EQUAL(BZM_CONTROL_FAULT_STATUS_INVALID, status.fault);
    init_result = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, control_test_init("1004"));
    TEST_ASSERT_EQUAL_UINT32(2, init_calls);
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
