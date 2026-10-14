#include "esp_attr.h"
#include "esp_log.h"
#include "bootloader_common.h"
#include "esp_rom_sys.h"
#include <stdarg.h>
#include <stdio.h>

#define RTC_LOG_MAGIC    0x80071062
#define RTC_LOG_BUFFER_SIZE (4 * 1024)

typedef struct {
    uint32_t magic;
    uint32_t checksum;
    uint32_t len;
} rtc_log_header_t;

#define RTC_LOG_BASE_ADDR   0x600FE000 /* Start of RTC Fast RAM on ESP32-S3 */
#define S_RTC_HEADER (*(rtc_log_header_t *)RTC_LOG_BASE_ADDR)
#define S_RTC_BUFFER ((char *)(RTC_LOG_BASE_ADDR + sizeof(rtc_log_header_t)))

static uint32_t calculate_rtc_checksum(const rtc_log_header_t *h)
{
    uint32_t checksum = h->magic;
    checksum ^= h->len;
    return checksum;
}

void rtc_write(const char *data, size_t len)
{
    if (S_RTC_HEADER.magic != RTC_LOG_MAGIC || S_RTC_HEADER.checksum != calculate_rtc_checksum(&S_RTC_HEADER)) {
        S_RTC_HEADER.magic = RTC_LOG_MAGIC;
        S_RTC_HEADER.len = 0;
        S_RTC_HEADER.checksum = calculate_rtc_checksum(&S_RTC_HEADER);
    }

    size_t available = RTC_LOG_BUFFER_SIZE - S_RTC_HEADER.len;
    size_t to_copy = (len < available) ? len : available;
    if (to_copy > 0) {
        memcpy(&S_RTC_BUFFER[S_RTC_HEADER.len], data, to_copy);
        S_RTC_HEADER.len += to_copy;
        S_RTC_HEADER.checksum = calculate_rtc_checksum(&S_RTC_HEADER);
    }
}

/* Character-level console interception via Archive Override */

typedef void (*putc_func_t)(char);

// Core ROM functions for character output on ESP32-S3
#define ROM_ETS_INSTALL_PUTC1  ((void (*)(putc_func_t))0x400005dc)
#define ROM_ETS_WRITE_CHAR_UART ((void (*)(char))0x400006b4)

extern void esp_rom_install_uart_printf(void);

/**
 * Custom character output function that writes to both the original
 * console channel (UART/USB) and our RTC log buffer.
 */
static void captured_putc(char c)
{
    // Forward to original hardware output (UART0 default)
    ROM_ETS_WRITE_CHAR_UART(c);
    
    // Capture to RTC RAM
    rtc_write(&c, 1);
}

/**
 * Capture-aware override of the bootloader's console initialization.
 * We will alias the standard 'bootloader_console_init' to this function
 * in the linker script.
 */
void capture_bootloader_console_init(void)
{
    // 1. Call standard ROM initialization for UART
    esp_rom_install_uart_printf();
    
    // 2. Hijack the character writer on channel 1
    ROM_ETS_INSTALL_PUTC1(captured_putc);
}
