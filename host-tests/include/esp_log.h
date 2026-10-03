#ifndef ESP_MINER_HOST_ESP_LOG_H
#define ESP_MINER_HOST_ESP_LOG_H

#include <stddef.h>

typedef enum {
    ESP_LOG_NONE = 0,
    ESP_LOG_ERROR,
    ESP_LOG_WARN,
    ESP_LOG_INFO,
    ESP_LOG_DEBUG,
    ESP_LOG_VERBOSE,
} esp_log_level_t;

#if defined(__GNUC__) || defined(__clang__)
#define ESP_LOG_PRINTF_FORMAT(format_index, first_argument) \
    __attribute__((format(printf, format_index, first_argument)))
#else
#define ESP_LOG_PRINTF_FORMAT(format_index, first_argument)
#endif

void esp_log_write(esp_log_level_t level, const char *tag, const char *format, ...)
    ESP_LOG_PRINTF_FORMAT(3, 4);
void host_esp_log_buffer_hex(esp_log_level_t level, const char *tag,
                             const void *buffer, size_t length);

#define ESP_LOGE(tag, ...) esp_log_write(ESP_LOG_ERROR, (tag), __VA_ARGS__)
#define ESP_LOGW(tag, ...) esp_log_write(ESP_LOG_WARN, (tag), __VA_ARGS__)
#define ESP_LOGI(tag, ...) esp_log_write(ESP_LOG_INFO, (tag), __VA_ARGS__)
#define ESP_LOGD(tag, ...) esp_log_write(ESP_LOG_DEBUG, (tag), __VA_ARGS__)
#define ESP_LOG_BUFFER_HEX(tag, buffer, length) \
    host_esp_log_buffer_hex(ESP_LOG_INFO, (tag), (buffer), (length))

#endif
