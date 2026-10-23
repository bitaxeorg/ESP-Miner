#include "bzm/gpio.h"
#include <math.h>
#include <pthread.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#define TACH_WINDOW_US 500000LL
#define TACH_STALE_US 2000000LL
#define FAN_MODE LEDC_LOW_SPEED_MODE
#define FAN_CHANNEL LEDC_CHANNEL_0
#define FAN_TIMER LEDC_TIMER_0
#define FAN_PWM_STEPS 1024U

static const char *TAG = "bonanza_gpio";
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static pthread_mutex_t fan_lock = PTHREAD_MUTEX_INITIALIZER;
static bool ready, pwm_ready;
static bzm_control_fault_t fault;
static int64_t tach_window_start_us, tach_sample_us;
static uint32_t tach_edges;
static uint16_t fan_rpm;
static uint8_t fan_percent = 100;
static uint8_t requested_fan_percent = 100;
static esp_timer_handle_t monitor_timer;

/* Called with lock held, including from the trip ISR. No PMBus transaction or
 * board mutex is needed to cut power. The GPIO ISR service is installed by main. */
static void safe_outputs_locked(void)
{
    gpio_set_level(BZM_GPIO_CORE_ENABLE, 0);
    gpio_set_level(BZM_GPIO_RESET, 0);
    gpio_set_level(BZM_GPIO_5V_ENABLE, 0);
    if (pwm_ready) ledc_stop(FAN_MODE, FAN_CHANNEL, 0);
    fan_percent = 100; /* Q1 inverts: a low gate releases the fan PWM high. */
}

static void check_locked(void)
{
    if (gpio_get_level(BZM_GPIO_TRIP)) {
        fault = BZM_CONTROL_FAULT_ASIC_TRIP;
        safe_outputs_locked();
    }
}

static void trip_isr(void *arg)
{
    (void)arg;
    portENTER_CRITICAL_ISR(&lock);
    /* Power-up can produce a trip edge while the ASICs are held in reset.
     * A sustained trip still fails check_locked(), including before reset
     * release. Once released, latch even a pulse that has already ended. */
    if (gpio_get_level(BZM_GPIO_RESET)) {
        fault = BZM_CONTROL_FAULT_ASIC_TRIP;
        safe_outputs_locked();
    }
    portEXIT_CRITICAL_ISR(&lock);
}

static void tach_isr(void *arg)
{
    (void)arg;
    portENTER_CRITICAL_ISR(&lock);
    ++tach_edges;
    portEXIT_CRITICAL_ISR(&lock);
}

static void monitor(void *arg)
{
    (void)arg;
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    check_locked();
    int64_t elapsed = now - tach_window_start_us;
    if (elapsed >= TACH_WINDOW_US) {
        uint64_t rpm = (uint64_t)tach_edges * 30000000ULL / elapsed; /* 2 pulses/rev */
        fan_rpm = rpm > UINT16_MAX ? UINT16_MAX : rpm;
        tach_edges = 0;
        tach_window_start_us = tach_sample_us = now;
    }
    portEXIT_CRITICAL(&lock);
}

esp_err_t bzm_gpio_init(void)
{
    if (ready) return ESP_OK;
    const uint64_t outputs = (1ULL << BZM_GPIO_RESET) | (1ULL << BZM_GPIO_5V_ENABLE) |
                             (1ULL << BZM_GPIO_CORE_ENABLE) | (1ULL << BZM_GPIO_FAN_PWM);
    gpio_set_level(BZM_GPIO_RESET, 0);
    gpio_set_level(BZM_GPIO_5V_ENABLE, 0);
    gpio_set_level(BZM_GPIO_CORE_ENABLE, 0);
    gpio_set_level(BZM_GPIO_FAN_PWM, 0);
    gpio_config_t config = {.pin_bit_mask = outputs, .mode = GPIO_MODE_INPUT_OUTPUT};
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "safe output configuration failed");
    config = (gpio_config_t){.pin_bit_mask = (1ULL << BZM_GPIO_TRIP) | (1ULL << BZM_GPIO_FAN_TACH) |
                                           (1ULL << BZM_GPIO_PGOOD), .mode = GPIO_MODE_INPUT};
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "input configuration failed");
    /* The level shifter is high impedance while its ASIC-side rail is off. */
    config = (gpio_config_t){.pin_bit_mask = 1ULL << BZM_GPIO_TRIP, .mode = GPIO_MODE_INPUT,
                            .pull_down_en = GPIO_PULLDOWN_ENABLE};
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "trip pull-down configuration failed");
    ledc_timer_config_t timer = {.speed_mode = FAN_MODE, .timer_num = FAN_TIMER,
        .duty_resolution = LEDC_TIMER_10_BIT, .freq_hz = 25000, .clk_cfg = LEDC_AUTO_CLK};
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "fan timer configuration failed");
    ledc_channel_config_t channel = {.gpio_num = BZM_GPIO_FAN_PWM, .speed_mode = FAN_MODE,
        .channel = FAN_CHANNEL, .timer_sel = FAN_TIMER, .duty = 0};
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "fan PWM configuration failed");
    pwm_ready = true;
    ESP_RETURN_ON_ERROR(ledc_stop(FAN_MODE, FAN_CHANNEL, 0), TAG, "full fan speed failed");
    ESP_RETURN_ON_ERROR(gpio_set_intr_type(BZM_GPIO_TRIP, GPIO_INTR_POSEDGE), TAG, "trip interrupt failed");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(BZM_GPIO_TRIP, trip_isr, NULL), TAG, "trip handler failed");
    ESP_RETURN_ON_ERROR(gpio_set_intr_type(BZM_GPIO_FAN_TACH, GPIO_INTR_NEGEDGE), TAG, "tach interrupt failed");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(BZM_GPIO_FAN_TACH, tach_isr, NULL), TAG, "tach handler failed");
    tach_window_start_us = esp_timer_get_time();
    esp_timer_create_args_t args = {.callback = monitor, .name = "bonanza_io"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&args, &monitor_timer), TAG, "monitor creation failed");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(monitor_timer, 100000), TAG, "monitor start failed");
    ready = true;
    return ESP_OK;
}

static void status_locked(bzm_control_status_t *status)
{
    bool five_volt = gpio_get_level(BZM_GPIO_5V_ENABLE) != 0;
    bool reset = gpio_get_level(BZM_GPIO_RESET) == 0;
    bool trip = gpio_get_level(BZM_GPIO_TRIP) != 0;
    bool core_enabled = gpio_get_level(BZM_GPIO_CORE_ENABLE) != 0;
    bool outputs_safe = !five_volt && reset && fan_percent == 100 && !core_enabled;
    *status = (bzm_control_status_t){
        .valid = ready,
        .state = fault != BZM_CONTROL_FAULT_NONE ? BZM_CONTROL_STATE_FAULT_LATCHED :
                 outputs_safe ? BZM_CONTROL_STATE_SAFE_OFF : BZM_CONTROL_STATE_CONTROLLED,
        .fault = fault, .core_enabled = core_enabled,
        .five_volt_enabled = five_volt, .asic_reset_asserted = reset,
        .fan_full = fan_percent == 100, .fan_percent = fan_percent, .trip_input_asserted = trip,
    };
}

enum command { STATUS, SAFE_OFF, CLEAR };
static esp_err_t safety_command(enum command command, bzm_control_status_t *status)
{
    if (status == NULL) return ESP_ERR_INVALID_ARG;
    *status = (bzm_control_status_t){.fault = BZM_CONTROL_FAULT_STATUS_INVALID};
    if (!ready) return ESP_ERR_INVALID_STATE;
    esp_err_t err = ESP_OK;
    portENTER_CRITICAL(&lock);
    check_locked();
    if (command == SAFE_OFF) safe_outputs_locked();
    if (command == CLEAR) {
        if (gpio_get_level(BZM_GPIO_TRIP) || gpio_get_level(BZM_GPIO_CORE_ENABLE) ||
            gpio_get_level(BZM_GPIO_PGOOD) || gpio_get_level(BZM_GPIO_5V_ENABLE) ||
            gpio_get_level(BZM_GPIO_RESET) || fan_percent != 100) err = ESP_ERR_INVALID_STATE;
        else fault = BZM_CONTROL_FAULT_NONE;
    }
    status_locked(status);
    portEXIT_CRITICAL(&lock);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Control command %u rejected: fault=%u trip=%u reset=%u 5v=%u",
                 (unsigned)command, (unsigned)status->fault, status->trip_input_asserted,
                 status->asic_reset_asserted, status->five_volt_enabled);
    }
    return err;
}

esp_err_t bzm_gpio_get_safety_status(bzm_control_status_t *s) { return safety_command(STATUS, s); }
esp_err_t bzm_gpio_safe_off(bzm_control_status_t *s) { return safety_command(SAFE_OFF, s); }
esp_err_t bzm_gpio_clear_safety_fault(bzm_control_status_t *s) { return safety_command(CLEAR, s); }

static esp_err_t set_output(gpio_num_t pin, bool enabled)
{
    if (!ready) return ESP_ERR_INVALID_STATE;
    esp_err_t err;
    portENTER_CRITICAL(&lock);
    check_locked();
    if (enabled && (fault != BZM_CONTROL_FAULT_NONE ||
        (pin == BZM_GPIO_RESET && !gpio_get_level(BZM_GPIO_5V_ENABLE)) ||
        (pin == BZM_GPIO_5V_ENABLE && (gpio_get_level(BZM_GPIO_RESET) || fan_percent != 100)))) err = ESP_ERR_INVALID_STATE;
    else if (pin == BZM_GPIO_CORE_ENABLE && !enabled) {
        safe_outputs_locked();
        err = ESP_OK;
    } else {
        err = ESP_OK;
        if (pin == BZM_GPIO_5V_ENABLE && !enabled) err = gpio_set_level(BZM_GPIO_RESET, 0);
        if (err == ESP_OK) err = gpio_set_level(pin, enabled);
    }
    portEXIT_CRITICAL(&lock);
    return err;
}

/* fan_lock serializes the retained request and PWM programming. */
static esp_err_t apply_fan_percent(uint8_t requested)
{
    uint32_t duty = (100U - requested) * FAN_PWM_STEPS / 100U;
    esp_err_t err = ledc_set_duty(FAN_MODE, FAN_CHANNEL, duty);
    portENTER_CRITICAL(&lock);
    check_locked();
    if (fault != BZM_CONTROL_FAULT_NONE || !gpio_get_level(BZM_GPIO_RESET)) requested = 100;
    if (err == ESP_OK) {
        err = requested == 100 ? ledc_stop(FAN_MODE, FAN_CHANNEL, 0) : ledc_update_duty(FAN_MODE, FAN_CHANNEL);
        if (err == ESP_OK) fan_percent = requested;
    }
    portEXIT_CRITICAL(&lock);
    return err;
}

esp_err_t bzm_gpio_set_5v_enabled(bool enabled) { return set_output(BZM_GPIO_5V_ENABLE, enabled); }
esp_err_t bzm_gpio_set_asic_reset(bool high)
{
    if (!ready) return ESP_ERR_INVALID_STATE;
    pthread_mutex_lock(&fan_lock);
    esp_err_t err = gpio_set_direction(BZM_GPIO_RESET, GPIO_MODE_INPUT_OUTPUT);
    if (err == ESP_OK) err = set_output(BZM_GPIO_RESET, high);
    if (err == ESP_OK && high) {
        err = apply_fan_percent(requested_fan_percent);
        if (err != ESP_OK) {
            portENTER_CRITICAL(&lock);
            safe_outputs_locked();
            portEXIT_CRITICAL(&lock);
        }
    }
    pthread_mutex_unlock(&fan_lock);
    return err;
}
esp_err_t bzm_gpio_set_core_enabled(bool enabled) { return set_output(BZM_GPIO_CORE_ENABLE, enabled); }

esp_err_t bzm_gpio_set_fan_percent(float percent)
{
    if (!isfinite(percent) || percent < 0 || percent > 1) return ESP_ERR_INVALID_ARG;
    if (!ready) return ESP_ERR_INVALID_STATE;
    uint8_t requested = lroundf(percent * 100);
    if (requested < 36) requested = 36;
    pthread_mutex_lock(&fan_lock);
    esp_err_t err = apply_fan_percent(requested);
    if (err == ESP_OK) requested_fan_percent = requested;
    pthread_mutex_unlock(&fan_lock);
    return err;
}

esp_err_t bzm_gpio_get_fan_rpm(uint16_t *rpm)
{
    if (rpm == NULL) return ESP_ERR_INVALID_ARG;
    *rpm = 0;
    if (!ready) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&lock);
    bool fresh = tach_sample_us != 0 && esp_timer_get_time() - tach_sample_us <= TACH_STALE_US;
    if (fresh) *rpm = fan_rpm;
    portEXIT_CRITICAL(&lock);
    return fresh ? ESP_OK : ESP_ERR_TIMEOUT;
}
