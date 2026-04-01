#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "log_buffer.h"
#include "websocket.h"

static const char * TAG = "log_buffer";

// Increment this when log_buffer_header_t struct changes
#define LOG_BUFFER_MAGIC 0xB17A5E10 

typedef struct {
    uint32_t magic;
    uint64_t total_written;
} log_buffer_header_t;

static EXT_RAM_NOINIT_ATTR log_buffer_header_t s_header;
static EXT_RAM_NOINIT_ATTR char s_buffer[LOG_BUFFER_SIZE];

static SemaphoreHandle_t s_log_mutex = NULL;

static void ring_write(const char *data, size_t len)
{
    if (len == 0) return;

    size_t write_offset = s_header.total_written % LOG_BUFFER_SIZE;
    size_t till_end = LOG_BUFFER_SIZE - write_offset;

    if (len <= till_end) {
        memcpy(&s_buffer[write_offset], data, len);
    } else {
        memcpy(&s_buffer[write_offset], data, till_end);
        memcpy(s_buffer, data + till_end, len - till_end);
    }

    s_header.total_written += len;
}

static int log_buffer_vprintf(const char *format, va_list args)
{
    if (s_log_mutex == NULL) {
        /* Fallback before init completes – just print to stdout */
        return vprintf(format, args);
    }

    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);

    if (needed < 0) return 0;

    char *output = (char *)heap_caps_malloc(needed + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!output) {
        output = (char *)heap_caps_malloc(needed + 1, MALLOC_CAP_SPIRAM);
    }

    if (output) {
        va_copy(args_copy, args);
        vsnprintf(output, needed + 1, format, args_copy);
        va_end(args_copy);

        if (xSemaphoreTakeRecursive(s_log_mutex, portMAX_DELAY) == pdTRUE) {
            ring_write(output, needed);
            websocket_log_notify();
            xSemaphoreGiveRecursive(s_log_mutex);
        }

        fputs(output, stdout);
        
        free(output);
    } else {
        /* Emergency fallback if absolutely out of memory */
        vprintf(format, args);
    }

    return needed;
}

void log_buffer_init(void)
{
    s_log_mutex = xSemaphoreCreateRecursiveMutex();
    if (s_log_mutex == NULL) {
        ESP_LOGW(TAG, "Failed to create recursive mutex");
        return;
    }

    if (s_header.magic != LOG_BUFFER_MAGIC) {
        memset(s_buffer, 0, LOG_BUFFER_SIZE);
        s_header.total_written = 0;
        s_header.magic = LOG_BUFFER_MAGIC;
        ESP_LOGI(TAG, "Cold boot, buffer initialized (%d KB)\n", LOG_BUFFER_SIZE / 1024);
    } else {
        ESP_LOGI(TAG, "Soft reboot detected, %" PRIu64 " bytes of logs preserved", s_header.total_written);
    }

    esp_log_set_vprintf(log_buffer_vprintf);
}

uint64_t log_buffer_get_total_written(void)
{
    if (s_log_mutex && xSemaphoreTakeRecursive(s_log_mutex, portMAX_DELAY) == pdTRUE) {
        uint64_t val = s_header.total_written;
        xSemaphoreGiveRecursive(s_log_mutex);
        return val;
    }
    return 0;
}

size_t log_buffer_read_absolute(uint64_t *abs_pos, char *dest, size_t max_len)
{
    if (s_log_mutex == NULL || abs_pos == NULL || dest == NULL || max_len == 0) {
        return 0;
    }

    if (xSemaphoreTakeRecursive(s_log_mutex, portMAX_DELAY) != pdTRUE) {
        return 0;
    }

    uint64_t total = s_header.total_written;
    uint64_t req_pos = *abs_pos;

    // Clamp position
    if (req_pos > total) {
        req_pos = total;
    }

    // Clamp on total buffer size
    if (total >= LOG_BUFFER_SIZE && req_pos < total - LOG_BUFFER_SIZE) {
        req_pos = total - LOG_BUFFER_SIZE;
        
        // Find next newline
        size_t available_scan = (size_t)(total - req_pos);
        for (size_t i = 0; i < available_scan; i++) {
            if (s_buffer[(req_pos + i) % LOG_BUFFER_SIZE] == '\n') {
                req_pos += i + 1;
                break;
            }
        }
    }

    size_t available = (size_t)(total - req_pos);
    size_t to_read = (available < max_len) ? available : max_len;

    if (to_read > 0) {
        size_t newline_idx = 0;
        for (size_t i = 0; i < to_read; i++) {
            if (s_buffer[(req_pos + i) % LOG_BUFFER_SIZE] == '\n') {
                newline_idx = i + 1; // inclusive of newline
                break;
            }
        }
        if (newline_idx > 0) {
            to_read = newline_idx;
        }

        size_t read_offset = req_pos % LOG_BUFFER_SIZE;
        size_t till_end = LOG_BUFFER_SIZE - read_offset;

        if (to_read <= till_end) {
            memcpy(dest, &s_buffer[read_offset], to_read);
        } else {
            memcpy(dest, &s_buffer[read_offset], till_end);
            memcpy(dest + till_end, s_buffer, to_read - till_end);
        }
    }

    *abs_pos = req_pos + to_read;

    xSemaphoreGiveRecursive(s_log_mutex);
    return to_read;
}
