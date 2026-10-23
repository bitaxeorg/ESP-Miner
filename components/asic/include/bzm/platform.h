#ifndef BZM_PLATFORM_H
#define BZM_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#include "bzm/status.h"

typedef struct {
    int64_t (*now_us)(void *context);
    void (*sleep_us)(void *context, uint32_t us);
    void *(*calloc)(void *context, size_t count, size_t size, bool large);
    void (*free)(void *context, void *allocation);
    /* Optional accelerator. Return failure without publishing a digest on error. */
    bzm_status_t (*sha256)(void *context, const uint8_t *data, size_t size, uint8_t digest[32]);
    void (*log)(void *context, char level, const char *tag, const char *format, va_list args);
    bzm_status_t (*serial_init)(void *context);
    bool (*serial_is_initialized)(void *context);
    bzm_status_t (*serial_set_baud)(void *context, int baud);
    bzm_status_t (*serial_wait_tx)(void *context, uint32_t timeout_ms);
    int (*serial_write)(void *context, const uint8_t *data, size_t size);
    int16_t (*serial_read)(void *context, uint8_t *data, uint16_t size, uint16_t timeout_ms);
    bzm_status_t (*serial_clear_rx)(void *context);
} bzm_platform_ops_t;

/* Bind once before starting driver threads. Context and callbacks must outlive
 * the driver. Rebinding while drivers or their allocations exist is invalid. */
bool bzm_platform_bind(const bzm_platform_ops_t *ops, void *context);
int64_t bzm_now_us(void);
void bzm_sleep_us(uint32_t us);
void bzm_sleep_ms(uint32_t ms);
void *bzm_calloc(size_t count, size_t size, bool large);
void bzm_free(void *allocation);
bzm_status_t bzm_platform_sha256(const uint8_t *data, size_t size, uint8_t digest[32]);
void bzm_log(char level, const char *tag, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
void bzm_log_hex(const char *tag, const void *data, size_t size);
bzm_status_t bzm_serial_init(void);
bool bzm_serial_is_initialized(void);
bzm_status_t bzm_serial_set_baud(int baud);
bzm_status_t bzm_serial_wait_tx(uint32_t timeout_ms);
int bzm_serial_write(const uint8_t *data, size_t size);
int16_t bzm_serial_read(uint8_t *data, uint16_t size, uint16_t timeout_ms);
bzm_status_t bzm_serial_clear_rx(void);

#define BZM_LOGE(tag, ...) bzm_log('E', tag, __VA_ARGS__)
#define BZM_LOGW(tag, ...) bzm_log('W', tag, __VA_ARGS__)
#define BZM_LOGI(tag, ...) bzm_log('I', tag, __VA_ARGS__)
#define BZM_LOGD(tag, ...) bzm_log('D', tag, __VA_ARGS__)
#define BZM_LOGV(tag, ...) bzm_log('V', tag, __VA_ARGS__)

#endif
