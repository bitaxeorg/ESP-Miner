#include <string.h>
#include "unity.h"
#include "device_pins.h"
#include "bzm/control.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"

static bzm_control_pins_t expected_pins;
static uint64_t configured_mask;
static unsigned hardware_calls;
static int levels[49];
static uint32_t pwm_duty;
static bool pwm_running;
static esp_err_t duty_error, update_error;
static bool trip_on_duty;
static int64_t clock_us;
static esp_timer_cb_t timer_callback;
static gpio_isr_t handlers[49];
static esp_err_t gpio_test_level(gpio_num_t pin, uint32_t level) { ++hardware_calls; levels[pin] = level; return ESP_OK; }
static int gpio_test_get(gpio_num_t pin) { ++hardware_calls; return levels[pin]; }
static esp_err_t gpio_test_config(const gpio_config_t *c) { ++hardware_calls; configured_mask |= c->pin_bit_mask; return ESP_OK; }
static esp_err_t gpio_test_intr(gpio_num_t pin, gpio_int_type_t type) { ++hardware_calls; (void)pin; (void)type; return ESP_OK; }
static esp_err_t gpio_test_handler(gpio_num_t pin, gpio_isr_t fn, void *arg)
{ ++hardware_calls; (void)arg; handlers[pin] = fn; return ESP_OK; }
static esp_err_t gpio_test_timer(const ledc_timer_config_t *c)
{
    ++hardware_calls;
    TEST_ASSERT_EQUAL_UINT32(25000, c->freq_hz);
    TEST_ASSERT_EQUAL(LEDC_TIMER_10_BIT, c->duty_resolution);
    return ESP_OK;
}
static esp_err_t gpio_test_channel(const ledc_channel_config_t *c)
{ ++hardware_calls; TEST_ASSERT_EQUAL_INT(expected_pins.fan_pwm, c->gpio_num); pwm_duty = c->duty; return ESP_OK; }
static esp_err_t gpio_test_stop(ledc_mode_t mode, ledc_channel_t channel, uint32_t idle)
{ ++hardware_calls; (void)mode; (void)channel; TEST_ASSERT_EQUAL_UINT32(0, idle); pwm_running = false; return ESP_OK; }
static esp_err_t gpio_test_duty(ledc_mode_t mode, ledc_channel_t channel, uint32_t duty)
{
    ++hardware_calls;
    (void)mode; (void)channel;
    if (trip_on_duty) handlers[expected_pins.trip](NULL);
    if (duty_error != ESP_OK) return duty_error;
    pwm_duty = duty;
    return ESP_OK;
}
static esp_err_t gpio_test_update(ledc_mode_t mode, ledc_channel_t channel)
{
    ++hardware_calls;
    (void)mode; (void)channel;
    if (update_error != ESP_OK) return update_error;
    pwm_running = true;
    return ESP_OK;
}
static int64_t gpio_test_time(void) { return clock_us; }
static esp_err_t gpio_test_create_timer(const esp_timer_create_args_t *args, esp_timer_handle_t *timer)
{ ++hardware_calls; timer_callback = args->callback; *timer = (void *)1; return ESP_OK; }
static esp_err_t gpio_test_start_timer(esp_timer_handle_t timer, uint64_t period)
{ ++hardware_calls; (void)timer; TEST_ASSERT_TRUE(period == 100000); return ESP_OK; }

static esp_err_t gpio_test_direction(gpio_num_t pin, gpio_mode_t mode)
{ ++hardware_calls; TEST_ASSERT_EQUAL_INT(expected_pins.reset, pin); TEST_ASSERT_EQUAL(GPIO_MODE_INPUT_OUTPUT, mode); return ESP_OK; }

#define gpio_set_direction gpio_test_direction
#define gpio_set_level gpio_test_level
#define gpio_get_level gpio_test_get
#define gpio_config gpio_test_config
#define gpio_set_intr_type gpio_test_intr
#define gpio_isr_handler_add gpio_test_handler
#define ledc_timer_config gpio_test_timer
#define ledc_channel_config gpio_test_channel
#define ledc_stop gpio_test_stop
#define ledc_set_duty gpio_test_duty
#define ledc_update_duty gpio_test_update
#define esp_timer_get_time gpio_test_time
#define esp_timer_create gpio_test_create_timer
#define esp_timer_start_periodic gpio_test_start_timer
#define bzm_gpio_init gpio_test_init
#define bzm_gpio_get_safety_status gpio_test_status
#define bzm_gpio_safe_off gpio_test_safe_off
#define bzm_gpio_clear_safety_fault gpio_test_clear
#define bzm_gpio_set_5v_enabled gpio_test_5v
#define bzm_gpio_set_asic_reset gpio_test_reset
#define bzm_gpio_set_core_enabled gpio_test_core
#define bzm_gpio_set_fan_percent gpio_test_fan
#define bzm_gpio_get_fan_rpm gpio_test_rpm
#include "../../bzm/port/esp/gpio.c"

static void gpio_fixture_with_pins(const bzm_control_pins_t *config)
{
    memset(levels, 0, sizeof(levels));
    memset(handlers, 0, sizeof(handlers));
    configured_mask = hardware_calls = 0;
    expected_pins = *config;
    ready = pwm_ready = pins_bound = false;
    fault = BZM_CONTROL_FAULT_NONE;
    tach_sample_us = 0;
    tach_edges = fan_rpm = 0;
    fan_percent = requested_fan_percent = 100;
    duty_error = update_error = ESP_OK;
    trip_on_duty = false;
    clock_us = 1000000;
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_init(config));
}

static void gpio_fixture(void)
{
    gpio_fixture_with_pins(&BONANZA_1004_CONTROL_PINS);
}

static void assert_safe(void)
{
    TEST_ASSERT_EQUAL_INT(0, levels[expected_pins.reset]);
    TEST_ASSERT_EQUAL_INT(0, levels[expected_pins.five_volt_enable]);
    TEST_ASSERT_EQUAL_INT(0, levels[expected_pins.core_enable]);
    TEST_ASSERT_FALSE(pwm_running);
    TEST_ASSERT_EQUAL_UINT8(100, fan_percent);
}

static void enable_outputs(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_core(true));
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_5v(true));
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_reset(true));
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_fan(0.5f));
}

TEST_CASE("Bonanza 1004 starts safe and preserves power and reset ordering without arming", "[asic][bonanza][gpio]")
{
    gpio_fixture();
    assert_safe();
    TEST_ASSERT_NOT_EQUAL(ESP_OK, gpio_test_reset(true));
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_fan(0.5f));
    assert_safe();
    bzm_control_status_t status;
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_status(&status));
    TEST_ASSERT_EQUAL(BZM_CONTROL_STATE_SAFE_OFF, status.state);
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_fan(1.0f));
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_status(&status));
    TEST_ASSERT_EQUAL(BZM_CONTROL_STATE_SAFE_OFF, status.state);
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_core(true));
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_status(&status));
    TEST_ASSERT_EQUAL(BZM_CONTROL_STATE_CONTROLLED, status.state);
    TEST_ASSERT_TRUE(status.core_enabled);
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_5v(true));
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_reset(true));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, gpio_test_5v(true));
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_5v(false));
    TEST_ASSERT_EQUAL_INT(0, levels[expected_pins.reset]);
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_safe_off(&status));
    assert_safe();
    TEST_ASSERT_FALSE(status.core_enabled);
    enable_outputs();
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_core(false));
    assert_safe();
}

TEST_CASE("Bonanza 1004 inverts fan PWM and expires stopped or stale tach samples", "[asic][bonanza][gpio]")
{
    gpio_fixture();
    enable_outputs();
    TEST_ASSERT_EQUAL_UINT32(512, pwm_duty);
    TEST_ASSERT_TRUE(pwm_running);
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_fan(0));
    TEST_ASSERT_EQUAL_UINT32(655, pwm_duty); /* 36% minimum at the fan. */
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_fan(1));
    TEST_ASSERT_EQUAL_UINT32(0, pwm_duty);
    TEST_ASSERT_FALSE(pwm_running);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, gpio_test_fan(NAN));
    uint16_t rpm = 999;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, gpio_test_rpm(&rpm));
    TEST_ASSERT_EQUAL_UINT16(0, rpm);
    for (unsigned edge = 0; edge < 40; ++edge) handlers[expected_pins.fan_tach](NULL);
    clock_us += 500000;
    timer_callback(NULL);
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_rpm(&rpm));
    TEST_ASSERT_EQUAL_UINT16(2400, rpm);
    clock_us += 500000;
    timer_callback(NULL);
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_rpm(&rpm));
    TEST_ASSERT_EQUAL_UINT16(0, rpm);
    clock_us += TACH_STALE_US + 1;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, gpio_test_rpm(&rpm));
}

TEST_CASE("Bonanza 1004 restores the saved fan request after startup and safe off", "[asic][bonanza][gpio]")
{
    gpio_fixture();
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_fan(0.5f));
    assert_safe();
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_core(true));
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_5v(true));
    TEST_ASSERT_FALSE(pwm_running);
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_reset(true));
    TEST_ASSERT_TRUE(pwm_running);
    TEST_ASSERT_EQUAL_UINT32(512, pwm_duty);
    bzm_control_status_t status;
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_status(&status));
    TEST_ASSERT_EQUAL_UINT8(50, status.fan_percent);
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_safe_off(&status));
    assert_safe();
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_core(true));
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_5v(true));
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_reset(true));
    TEST_ASSERT_TRUE(pwm_running);
    TEST_ASSERT_EQUAL_UINT32(512, pwm_duty);
    TEST_ASSERT_EQUAL_UINT8(50, fan_percent);
}

TEST_CASE("Bonanza 1004 fails safe if restoring the fan request fails", "[asic][bonanza][gpio]")
{
    for (unsigned stage = 0; stage < 2; ++stage) {
        gpio_fixture();
        TEST_ASSERT_EQUAL(ESP_OK, gpio_test_fan(0.5f));
        TEST_ASSERT_EQUAL(ESP_OK, gpio_test_core(true));
        TEST_ASSERT_EQUAL(ESP_OK, gpio_test_5v(true));
        if (stage == 0) duty_error = ESP_FAIL;
        else update_error = ESP_FAIL;
        TEST_ASSERT_EQUAL(ESP_FAIL, gpio_test_reset(true));
        assert_safe();
        duty_error = update_error = ESP_OK;
        TEST_ASSERT_EQUAL(ESP_OK, gpio_test_core(true));
        TEST_ASSERT_EQUAL(ESP_OK, gpio_test_5v(true));
        TEST_ASSERT_EQUAL(ESP_OK, gpio_test_reset(true));
        TEST_ASSERT_EQUAL_UINT8(50, fan_percent);
    }
}

TEST_CASE("Bonanza 1004 fan restoration cannot override an ASIC trip", "[asic][bonanza][gpio]")
{
    gpio_fixture();
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_fan(0.5f));
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_core(true));
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_5v(true));
    trip_on_duty = true;
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_reset(true));
    assert_safe();
    bzm_control_status_t status;
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_status(&status));
    TEST_ASSERT_EQUAL(BZM_CONTROL_FAULT_ASIC_TRIP, status.fault);
}

TEST_CASE("Bonanza 1004 trip interrupt cuts power and latches even a brief trip", "[asic][bonanza][gpio]")
{
    gpio_fixture();
    enable_outputs();
    handlers[expected_pins.trip](NULL); /* Pulse has already ended. */
    assert_safe();
    bzm_control_status_t status;
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_status(&status));
    TEST_ASSERT_EQUAL(BZM_CONTROL_FAULT_ASIC_TRIP, status.fault);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, gpio_test_core(true));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, gpio_test_5v(true));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, gpio_test_reset(true));
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_fan(0.5f));
    assert_safe();
    levels[expected_pins.trip] = 1;
    TEST_ASSERT_NOT_EQUAL(ESP_OK, gpio_test_clear(&status));
    levels[expected_pins.trip] = 0;
    levels[expected_pins.pgood] = 1;
    TEST_ASSERT_NOT_EQUAL(ESP_OK, gpio_test_clear(&status));
    levels[expected_pins.pgood] = 0;
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_clear(&status));
    assert_safe();
    enable_outputs();
}

TEST_CASE("Bonanza 1004 tolerates reset-held power-up edges but rejects an asserted trip", "[asic][bonanza][gpio]")
{
    gpio_fixture();
    bzm_control_status_t status;
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_core(true));
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_5v(true));
    handlers[expected_pins.trip](NULL);
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_status(&status));
    TEST_ASSERT_EQUAL(BZM_CONTROL_FAULT_NONE, status.fault);
    TEST_ASSERT_TRUE(status.five_volt_enabled);
    TEST_ASSERT_TRUE(status.asic_reset_asserted);
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_reset(true));
    handlers[expected_pins.trip](NULL);
    assert_safe();

    gpio_fixture();
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_core(true));
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_5v(true));
    levels[expected_pins.trip] = 1;
    handlers[expected_pins.trip](NULL);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, gpio_test_reset(true));
    assert_safe();
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_status(&status));
    TEST_ASSERT_EQUAL(BZM_CONTROL_FAULT_ASIC_TRIP, status.fault);
}

TEST_CASE("Bonanza 1004 outputs persist without host traffic until explicit safe off", "[asic][bonanza][gpio]")
{
    gpio_fixture();
    enable_outputs();
    clock_us += 600000000;
    timer_callback(NULL);
    TEST_ASSERT_EQUAL_INT(1, levels[expected_pins.reset]);
    TEST_ASSERT_EQUAL_INT(1, levels[expected_pins.five_volt_enable]);
    TEST_ASSERT_EQUAL_INT(1, levels[expected_pins.core_enable]);
    TEST_ASSERT_TRUE(pwm_running);
    bzm_control_status_t status;
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_status(&status));
    TEST_ASSERT_EQUAL(BZM_CONTROL_STATE_CONTROLLED, status.state);
    TEST_ASSERT_EQUAL(BZM_CONTROL_FAULT_NONE, status.fault);
    TEST_ASSERT_EQUAL_UINT8(50, status.fan_percent);
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_fan(0.6f));
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_safe_off(&status));
    TEST_ASSERT_EQUAL(BZM_CONTROL_STATE_SAFE_OFF, status.state);
    assert_safe();
}

TEST_CASE("Bonanza GPIO uses configured pins for outputs fan status and trip protection", "[asic][bonanza][gpio]")
{
    const bzm_control_pins_t remapped = {
        .reset = 12, .trip = 13, .fan_pwm = 14, .fan_tach = 15,
        .core_enable = 16, .pgood = 19, .five_volt_enable = 20,
    };
    gpio_fixture_with_pins(&remapped);
    uint64_t mask = (1ULL << remapped.reset) | (1ULL << remapped.trip) |
        (1ULL << remapped.fan_pwm) | (1ULL << remapped.fan_tach) |
        (1ULL << remapped.core_enable) | (1ULL << remapped.pgood) |
        (1ULL << remapped.five_volt_enable);
    TEST_ASSERT_TRUE_MESSAGE(mask == configured_mask, "GPIO configuration must use only the configured control pins");
    TEST_ASSERT_NOT_NULL(handlers[remapped.trip]);
    TEST_ASSERT_NOT_NULL(handlers[remapped.fan_tach]);
    assert_safe();
    enable_outputs();
    bzm_control_status_t status;
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_status(&status));
    TEST_ASSERT_TRUE(status.core_enabled);
    TEST_ASSERT_TRUE(status.five_volt_enabled);
    TEST_ASSERT_FALSE(status.asic_reset_asserted);
    handlers[remapped.trip](NULL);
    assert_safe();
    levels[remapped.pgood] = 1;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, gpio_test_clear(&status));
    levels[remapped.pgood] = 0;
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_clear(&status));
    for (unsigned edge = 0; edge < 40; ++edge) handlers[remapped.fan_tach](NULL);
    clock_us += 500000;
    timer_callback(NULL);
    uint16_t rpm;
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_rpm(&rpm));
    TEST_ASSERT_EQUAL_UINT16(2400, rpm);
}

TEST_CASE("Bonanza GPIO rejects missing invalid or duplicate pins before setup", "[asic][bonanza][gpio]")
{
    for (unsigned invalid = 0; invalid < 5; ++invalid) {
        ready = pwm_ready = pins_bound = false;
        hardware_calls = 0;
        bzm_control_pins_t bad = BONANZA_1004_CONTROL_PINS;
        if (invalid == 1) bad.trip = -1;
        if (invalid == 2) bad.pgood = GPIO_NUM_MAX;
        if (invalid == 3) bad.fan_tach = bad.reset;
        if (invalid == 4) bad.core_enable = 22;
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, gpio_test_init(invalid == 0 ? NULL : &bad));
        TEST_ASSERT_EQUAL_UINT32(0, hardware_calls);
        TEST_ASSERT_FALSE(pins_bound);
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, gpio_test_core(true));
    }
}

TEST_CASE("Bonanza GPIO retains its map and rejects retargeting after initialization", "[asic][bonanza][gpio]")
{
    bzm_control_pins_t config = BONANZA_1004_CONTROL_PINS;
    gpio_fixture_with_pins(&config);
    unsigned calls = hardware_calls;
    TEST_ASSERT_EQUAL(ESP_OK, gpio_test_init(&config));
    TEST_ASSERT_EQUAL_UINT32(calls, hardware_calls);
    config.reset = GPIO_NUM_12;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, gpio_test_init(&config));
    TEST_ASSERT_EQUAL_UINT32(calls, hardware_calls);
    enable_outputs();
    TEST_ASSERT_EQUAL_INT(1, levels[expected_pins.reset]);
    TEST_ASSERT_EQUAL_INT(0, levels[config.reset]);
    handlers[expected_pins.trip](NULL);
    assert_safe();
}
