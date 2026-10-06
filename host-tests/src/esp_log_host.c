#include "esp_log.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

static const char *level_name(esp_log_level_t level)
{
    switch (level) {
        case ESP_LOG_ERROR:
            return "E";
        case ESP_LOG_WARN:
            return "W";
        case ESP_LOG_DEBUG:
            return "D";
        case ESP_LOG_VERBOSE:
            return "V";
        case ESP_LOG_INFO:
        case ESP_LOG_NONE:
        default:
            return "I";
    }
}

void esp_log_write(esp_log_level_t level, const char *tag, const char *format, ...)
{
    va_list arguments;

    fprintf(stderr, "%s (%s): ", level_name(level), tag != NULL ? tag : "");
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
}

void host_esp_log_buffer_hex(esp_log_level_t level, const char *tag,
                             const void *buffer, size_t length)
{
    const uint8_t *bytes = buffer;

    fprintf(stderr, "%s (%s):", level_name(level), tag != NULL ? tag : "");
    for (size_t index = 0; index < length; ++index) {
        fprintf(stderr, " %02x", bytes[index]);
    }
    fputc('\n', stderr);
}
