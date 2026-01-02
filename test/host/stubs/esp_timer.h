/**
 * @file esp_timer.h
 * @brief ESP-IDF timer stub for host-based testing
 */

#ifndef ESP_TIMER_H_STUB
#define ESP_TIMER_H_STUB

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get time in microseconds since boot
 * @return Time in microseconds
 */
static inline int64_t esp_timer_get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + (int64_t)ts.tv_nsec / 1000LL;
}

/* Timer handle type */
typedef struct esp_timer* esp_timer_handle_t;

/* Timer callback type */
typedef void (*esp_timer_cb_t)(void* arg);

/* Timer configuration */
typedef struct {
    esp_timer_cb_t callback;
    void* arg;
    const char* name;
    bool skip_unhandled_events;
} esp_timer_create_args_t;

/* Timer creation - stub */
static inline int esp_timer_create(const esp_timer_create_args_t* create_args, esp_timer_handle_t* out_handle)
{
    (void)create_args;
    *out_handle = NULL;
    return 0;
}

/* Timer start - stub */
static inline int esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period_us)
{
    (void)timer;
    (void)period_us;
    return 0;
}

static inline int esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us)
{
    (void)timer;
    (void)timeout_us;
    return 0;
}

/* Timer stop - stub */
static inline int esp_timer_stop(esp_timer_handle_t timer)
{
    (void)timer;
    return 0;
}

/* Timer delete - stub */
static inline int esp_timer_delete(esp_timer_handle_t timer)
{
    (void)timer;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* ESP_TIMER_H_STUB */
