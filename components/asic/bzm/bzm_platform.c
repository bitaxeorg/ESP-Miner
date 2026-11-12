#include "bzm/platform.h"
#include <stdlib.h>

static bzm_platform_ops_t platform;
static void *platform_context;

bool bzm_platform_bind(const bzm_platform_ops_t *ops, void *context)
{
    if (ops == NULL || ops->now_us == NULL || ops->sleep_us == NULL ||
        (ops->calloc == NULL) != (ops->free == NULL)) return false;
    platform = *ops;
    platform_context = context;
    return true;
}

int64_t bzm_now_us(void) { return platform.now_us ? platform.now_us(platform_context) : 0; }
void bzm_sleep_us(uint32_t us) { if (platform.sleep_us) platform.sleep_us(platform_context, us); }
void bzm_sleep_ms(uint32_t ms)
{
    while (ms > UINT32_MAX / 1000U) {
        bzm_sleep_us((UINT32_MAX / 1000U) * 1000U);
        ms -= UINT32_MAX / 1000U;
    }
    bzm_sleep_us(ms * 1000U);
}
void *bzm_calloc(size_t count, size_t size, bool large)
{
    return platform.calloc ? platform.calloc(platform_context, count, size, large) : calloc(count, size);
}
void bzm_free(void *allocation)
{
    if (platform.free) platform.free(platform_context, allocation);
    else free(allocation);
}
bzm_status_t bzm_platform_sha256(const uint8_t *data, size_t size, uint8_t digest[32])
{
    return platform.sha256 ? platform.sha256(platform_context, data, size, digest) : BZM_ERR_NOT_SUPPORTED;
}
void bzm_log(char level, const char *tag, const char *format, ...)
{
    if (!platform.log) return;
    va_list args;
    va_start(args, format);
    platform.log(platform_context, level, tag, format, args);
    va_end(args);
}
void bzm_log_hex(const char *tag, const void *data, size_t size)
{
    const uint8_t *bytes = data;
    for (size_t offset = 0; offset < size; offset += 16) {
        char line[49];
        size_t n = size - offset < 16 ? size - offset : 16;
        for (size_t i = 0; i < n; ++i) {
            static const char hex[] = "0123456789abcdef";
            line[3 * i] = hex[bytes[offset + i] >> 4];
            line[3 * i + 1] = hex[bytes[offset + i] & 15];
            line[3 * i + 2] = ' ';
        }
        line[3 * n] = 0;
        BZM_LOGD(tag, "%s", line);
    }
}
bzm_status_t bzm_serial_init(void)
{
    if (!platform.now_us || !platform.sleep_us) return BZM_ERR_INVALID_STATE;
    return platform.serial_init ? platform.serial_init(platform_context) : BZM_ERR_NOT_SUPPORTED;
}
bool bzm_serial_is_initialized(void) { return platform.serial_is_initialized && platform.serial_is_initialized(platform_context); }
bzm_status_t bzm_serial_set_baud(int baud) { return platform.serial_set_baud ? platform.serial_set_baud(platform_context, baud) : BZM_ERR_NOT_SUPPORTED; }
bzm_status_t bzm_serial_wait_tx(uint32_t ms) { return platform.serial_wait_tx ? platform.serial_wait_tx(platform_context, ms) : BZM_ERR_NOT_SUPPORTED; }
int bzm_serial_write(const uint8_t *data, size_t size) { return platform.serial_write ? platform.serial_write(platform_context, data, size) : -1; }
int16_t bzm_serial_read(uint8_t *data, uint16_t size, uint16_t ms) { return platform.serial_read ? platform.serial_read(platform_context, data, size, ms) : -1; }
bzm_status_t bzm_serial_clear_rx(void) { return platform.serial_clear_rx ? platform.serial_clear_rx(platform_context) : BZM_ERR_NOT_SUPPORTED; }
