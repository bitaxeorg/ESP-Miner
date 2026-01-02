/**
 * @file autotune_state.h
 * @brief Thread-safe autotune state management
 *
 * This module provides a thread-safe container for autotune state,
 * replacing static variables in the original autotuneOffset function.
 * All state access is protected by a mutex.
 */

#ifndef AUTOTUNE_STATE_H
#define AUTOTUNE_STATE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque autotune state handle
 */
typedef struct autotune_state* autotune_state_t;

/**
 * @brief Snapshot of readings from various modules
 *
 * This struct is used to atomically capture readings that would
 * otherwise be read unsafely from GlobalState.
 */
typedef struct {
    float chip_temp_avg;
    float frequency_value;
    float fan_perc;
    float current_hashrate;
    float power;
    uint16_t voltage_mv;
} autotune_readings_t;

/**
 * @brief Create and initialize autotune state
 * @return Handle to autotune state, or NULL on failure
 */
autotune_state_t autotune_state_create(void);

/**
 * @brief Destroy autotune state and free resources
 * @param state Handle to destroy
 */
void autotune_state_destroy(autotune_state_t state);

/**
 * @brief Reset all state to initial values
 * @param state Handle to reset
 */
void autotune_state_reset(autotune_state_t state);

/**
 * @brief Get milliseconds since last autotune adjustment
 * @param state Handle
 * @param current_tick_ms Current tick count in milliseconds
 * @return Milliseconds since last adjustment
 */
uint32_t autotune_state_get_ms_since_last_adjust(autotune_state_t state,
                                                  uint32_t current_tick_ms);

/**
 * @brief Update the last adjustment time
 * @param state Handle
 * @param current_tick_ms Current tick count in milliseconds
 */
void autotune_state_update_last_adjust_time(autotune_state_t state,
                                             uint32_t current_tick_ms);

/**
 * @brief Get consecutive low hashrate count
 * @param state Handle
 * @return Current count
 */
uint8_t autotune_state_get_low_hashrate_count(autotune_state_t state);

/**
 * @brief Increment consecutive low hashrate count
 * @param state Handle
 * @return New count after increment
 */
uint8_t autotune_state_increment_low_hashrate(autotune_state_t state);

/**
 * @brief Reset consecutive low hashrate count to zero
 * @param state Handle
 */
void autotune_state_reset_low_hashrate(autotune_state_t state);

/**
 * @brief Check if state has been initialized
 * @param state Handle
 * @return true if initialized and ready to use
 */
bool autotune_state_is_valid(autotune_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* AUTOTUNE_STATE_H */
