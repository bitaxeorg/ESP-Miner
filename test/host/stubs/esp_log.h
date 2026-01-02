/**
 * @file esp_log.h
 * @brief ESP-IDF logging stub for host-based testing
 *
 * Redirects ESP_LOG* macros to printf for host testing.
 * Can be configured to capture logs for test assertions.
 */

#ifndef ESP_LOG_H_STUB
#define ESP_LOG_H_STUB

#include <stdio.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_LOG_NONE,       /*!< No log output */
    ESP_LOG_ERROR,      /*!< Critical errors, software module can not recover on its own */
    ESP_LOG_WARN,       /*!< Error conditions from which recovery measures have been taken */
    ESP_LOG_INFO,       /*!< Information messages which describe normal flow of events */
    ESP_LOG_DEBUG,      /*!< Extra information which is not necessary for normal use (values, pointers, sizes, etc). */
    ESP_LOG_VERBOSE     /*!< Bigger chunks of debugging information, or frequent messages which can potentially flood the output. */
} esp_log_level_t;

/* Log level colors for terminal output */
#define LOG_COLOR_BLACK   "30"
#define LOG_COLOR_RED     "31"
#define LOG_COLOR_GREEN   "32"
#define LOG_COLOR_BROWN   "33"
#define LOG_COLOR_BLUE    "34"
#define LOG_COLOR_PURPLE  "35"
#define LOG_COLOR_CYAN    "36"
#define LOG_COLOR(COLOR)  "\033[0;" COLOR "m"
#define LOG_BOLD(COLOR)   "\033[1;" COLOR "m"
#define LOG_RESET_COLOR   "\033[0m"

#define LOG_COLOR_E       LOG_COLOR(LOG_COLOR_RED)
#define LOG_COLOR_W       LOG_COLOR(LOG_COLOR_BROWN)
#define LOG_COLOR_I       LOG_COLOR(LOG_COLOR_GREEN)
#define LOG_COLOR_D
#define LOG_COLOR_V

/* Configuration for test mode */
#ifndef ESP_LOG_TEST_MODE
#define ESP_LOG_TEST_MODE 0
#endif

#if ESP_LOG_TEST_MODE
/* In test mode, logs can be captured */
void esp_log_test_capture_start(void);
void esp_log_test_capture_stop(void);
const char* esp_log_test_get_last_message(void);
int esp_log_test_get_message_count(void);
void esp_log_test_clear(void);
#endif

/* Stub implementation - just prints to stdout */
#define ESP_LOGE(tag, format, ...) \
    printf(LOG_COLOR_E "E (%s) " format LOG_RESET_COLOR "\n", tag, ##__VA_ARGS__)

#define ESP_LOGW(tag, format, ...) \
    printf(LOG_COLOR_W "W (%s) " format LOG_RESET_COLOR "\n", tag, ##__VA_ARGS__)

#define ESP_LOGI(tag, format, ...) \
    printf(LOG_COLOR_I "I (%s) " format LOG_RESET_COLOR "\n", tag, ##__VA_ARGS__)

#define ESP_LOGD(tag, format, ...) \
    printf("D (%s) " format "\n", tag, ##__VA_ARGS__)

#define ESP_LOGV(tag, format, ...) \
    printf("V (%s) " format "\n", tag, ##__VA_ARGS__)

/* Early log macros (before scheduler starts) - same behavior on host */
#define ESP_EARLY_LOGE(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)
#define ESP_EARLY_LOGW(tag, format, ...) ESP_LOGW(tag, format, ##__VA_ARGS__)
#define ESP_EARLY_LOGI(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#define ESP_EARLY_LOGD(tag, format, ...) ESP_LOGD(tag, format, ##__VA_ARGS__)
#define ESP_EARLY_LOGV(tag, format, ...) ESP_LOGV(tag, format, ##__VA_ARGS__)

/* Log level setting stubs */
static inline void esp_log_level_set(const char* tag, esp_log_level_t level) {
    (void)tag;
    (void)level;
}

static inline esp_log_level_t esp_log_level_get(const char* tag) {
    (void)tag;
    return ESP_LOG_VERBOSE;
}

#ifdef __cplusplus
}
#endif

#endif /* ESP_LOG_H_STUB */
