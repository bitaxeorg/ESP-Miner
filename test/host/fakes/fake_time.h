/**
 * @file fake_time.h
 * @brief Controllable time for deterministic testing
 *
 * Allows tests to control the "current time" for reproducible results.
 */

#ifndef FAKE_TIME_H
#define FAKE_TIME_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize fake time system
 * Starts at time 0
 */
void fake_time_init(void);

/**
 * @brief Set the current fake time in microseconds
 */
void fake_time_set_us(int64_t time_us);

/**
 * @brief Get the current fake time in microseconds
 */
int64_t fake_time_get_us(void);

/**
 * @brief Advance time by specified microseconds
 */
void fake_time_advance_us(int64_t delta_us);

/**
 * @brief Advance time by specified milliseconds
 */
void fake_time_advance_ms(int64_t delta_ms);

/**
 * @brief Advance time by specified seconds
 */
void fake_time_advance_sec(int64_t delta_sec);

/**
 * @brief Set tick count for FreeRTOS compatibility
 */
void fake_time_set_ticks(uint32_t ticks);

/**
 * @brief Get tick count
 */
uint32_t fake_time_get_ticks(void);

/**
 * @brief Enable/disable fake time mode
 * When disabled, real system time is used
 */
void fake_time_enable(bool enable);

/**
 * @brief Check if fake time is enabled
 */
bool fake_time_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* FAKE_TIME_H */
