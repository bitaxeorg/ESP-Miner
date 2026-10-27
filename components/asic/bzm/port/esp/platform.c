#include "bzm/platform.h"
#include "bzm/control.h"
#include "bzm/serial.h"
#include "serial.h"
#include "bzm/esp.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "psa/crypto.h"

static int64_t now_us(void *ctx) { (void)ctx; return esp_timer_get_time(); }
static void sleep_us(void *ctx, uint32_t us)
{
    (void)ctx;
    if (us < 1000) esp_rom_delay_us(us);
    else {
        TickType_t ticks = pdMS_TO_TICKS(us / 1000U);
        vTaskDelay(ticks ? ticks : 1);
    }
}
static void *allocate(void *ctx, size_t count, size_t size, bool large)
{
    (void)ctx;
#if CONFIG_SPIRAM
    if (large && !esp_psram_is_initialized()) return NULL;
#else
    if (large) return NULL;
#endif
    return heap_caps_calloc(count, size, large ? MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT : MALLOC_CAP_DEFAULT);
}
static void release(void *ctx, void *ptr) { (void)ctx; heap_caps_free(ptr); }
static bzm_status_t sha256(void *ctx, const uint8_t *data, size_t size, uint8_t digest[32])
{
    (void)ctx;
    size_t written = 0;
    psa_status_t status = psa_hash_compute(PSA_ALG_SHA_256, data, size, digest, 32, &written);
    return status == PSA_SUCCESS && written == 32 ? BZM_OK : BZM_ERR_IO;
}
static void log_message(void *ctx, char level, const char *tag, const char *format, va_list args)
{
    (void)ctx;
    esp_log_level_t severity = level == 'E' ? ESP_LOG_ERROR : level == 'W' ? ESP_LOG_WARN :
                               level == 'I' ? ESP_LOG_INFO : level == 'D' ? ESP_LOG_DEBUG : ESP_LOG_VERBOSE;
    esp_log_writev(severity, tag, format, args);
    esp_log_write(severity, tag, "\n");
}
static bzm_status_t serial_init(void *ctx) { (void)ctx; return BZM_SERIAL_init(); }
static bool serial_ready(void *ctx) { (void)ctx; return SERIAL_is_initialized(); }
static bzm_status_t serial_baud(void *ctx, int baud) { (void)ctx; return BZM_SERIAL_set_baud(baud); }
static bzm_status_t serial_wait(void *ctx, uint32_t ms) { (void)ctx; return BZM_SERIAL_wait_tx_done(ms); }
static int serial_write(void *ctx, const uint8_t *data, size_t size)
{ (void)ctx; return SERIAL_send((uint8_t *)data, (int)size, false); }
static int16_t serial_read(void *ctx, uint8_t *data, uint16_t size, uint16_t ms)
{ (void)ctx; return BZM_SERIAL_rx(data, size, ms); }
static bzm_status_t serial_clear(void *ctx) { (void)ctx; return uart_flush_input(UART_NUM_1); }

static bzm_status_t control_init(void *ctx, const char * board_version) { (void)ctx; return BZM_control_init(board_version, NULL); }

static bzm_status_t control_get_safety_status(void *ctx, bzm_control_status_t * status) { (void)ctx; return BZM_control_get_safety_status(status); }

static bzm_status_t control_clear_safety_fault(void *ctx, bzm_control_status_t * status) { (void)ctx; return BZM_control_clear_safety_fault(status); }

static bzm_status_t control_safe_off(void *ctx, bzm_control_status_t * status) { (void)ctx; return BZM_control_safe_off(status); }

static bzm_status_t control_set_5v_enabled(void *ctx, bool enabled) { (void)ctx; return BZM_control_set_5v_enabled(enabled); }

static bzm_status_t control_set_asic_reset(void *ctx, bool high) { (void)ctx; return BZM_control_set_asic_reset(high); }

static bzm_status_t control_set_fan_percent(void *ctx, float percent) { (void)ctx; return BZM_control_set_fan_percent(percent); }

static bzm_status_t control_get_fan_rpm(void *ctx, uint16_t * rpm) { (void)ctx; return BZM_control_get_fan_rpm(rpm); }

static bzm_status_t control_set_core_enabled(void *ctx, bool enabled) { (void)ctx; return BZM_control_set_core_enabled(enabled); }

bool bzm_platform_esp_init(void)
{
    static bool initialized;
    if (initialized) return true;
    const bzm_platform_ops_t platform = {
        .now_us = now_us, .sleep_us = sleep_us, .calloc = allocate, .free = release,
        .sha256 = sha256, .log = log_message,
        .serial_init = serial_init, .serial_is_initialized = serial_ready, .serial_set_baud = serial_baud,
        .serial_wait_tx = serial_wait, .serial_write = serial_write, .serial_read = serial_read,
        .serial_clear_rx = serial_clear,
    };
    const bzm_control_ops_t controls = {
        .init = control_init,
        .get_safety_status = control_get_safety_status,
        .clear_safety_fault = control_clear_safety_fault,
        .safe_off = control_safe_off,
        .set_5v_enabled = control_set_5v_enabled,
        .set_asic_reset = control_set_asic_reset,
        .set_fan_percent = control_set_fan_percent,
        .get_fan_rpm = control_get_fan_rpm,
        .set_core_enabled = control_set_core_enabled,
    };
    initialized = bzm_platform_bind(&platform, NULL) && bzm_control_bind(&controls, NULL);
    return initialized;
}
