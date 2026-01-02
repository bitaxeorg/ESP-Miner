/**
 * @file autotune_state.c
 * @brief Thread-safe autotune state management implementation
 *
 * This module provides mutex-protected access to autotune state variables,
 * fixing the race conditions in the original autotuneOffset function.
 */

#include "autotune_state.h"
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/**
 * @brief Internal state structure
 */
struct autotune_state {
    SemaphoreHandle_t mutex;
    uint32_t last_adjust_tick_ms;
    uint8_t consecutive_low_hashrate;
    bool initialized;
};

/* ============================================
 * Lifecycle Functions
 * ============================================ */

autotune_state_t autotune_state_create(void)
{
    struct autotune_state* state = calloc(1, sizeof(struct autotune_state));
    if (!state) {
        return NULL;
    }

    state->mutex = xSemaphoreCreateMutex();
    if (!state->mutex) {
        free(state);
        return NULL;
    }

    state->last_adjust_tick_ms = 0;
    state->consecutive_low_hashrate = 0;
    state->initialized = true;

    return state;
}

void autotune_state_destroy(autotune_state_t state)
{
    if (!state) {
        return;
    }

    if (state->mutex) {
        vSemaphoreDelete(state->mutex);
    }

    state->initialized = false;
    free(state);
}

void autotune_state_reset(autotune_state_t state)
{
    if (!state || !state->mutex) {
        return;
    }

    if (xSemaphoreTake(state->mutex, portMAX_DELAY) == pdTRUE) {
        state->last_adjust_tick_ms = 0;
        state->consecutive_low_hashrate = 0;
        xSemaphoreGive(state->mutex);
    }
}

/* ============================================
 * Timing Functions
 * ============================================ */

uint32_t autotune_state_get_ms_since_last_adjust(autotune_state_t state,
                                                  uint32_t current_tick_ms)
{
    if (!state || !state->mutex) {
        return 0;
    }

    uint32_t result = 0;

    if (xSemaphoreTake(state->mutex, portMAX_DELAY) == pdTRUE) {
        /* Handle wraparound correctly */
        result = current_tick_ms - state->last_adjust_tick_ms;
        xSemaphoreGive(state->mutex);
    }

    return result;
}

void autotune_state_update_last_adjust_time(autotune_state_t state,
                                             uint32_t current_tick_ms)
{
    if (!state || !state->mutex) {
        return;
    }

    if (xSemaphoreTake(state->mutex, portMAX_DELAY) == pdTRUE) {
        state->last_adjust_tick_ms = current_tick_ms;
        xSemaphoreGive(state->mutex);
    }
}

/* ============================================
 * Low Hashrate Tracking
 * ============================================ */

uint8_t autotune_state_get_low_hashrate_count(autotune_state_t state)
{
    if (!state || !state->mutex) {
        return 0;
    }

    uint8_t result = 0;

    if (xSemaphoreTake(state->mutex, portMAX_DELAY) == pdTRUE) {
        result = state->consecutive_low_hashrate;
        xSemaphoreGive(state->mutex);
    }

    return result;
}

uint8_t autotune_state_increment_low_hashrate(autotune_state_t state)
{
    if (!state || !state->mutex) {
        return 0;
    }

    uint8_t result = 0;

    if (xSemaphoreTake(state->mutex, portMAX_DELAY) == pdTRUE) {
        if (state->consecutive_low_hashrate < 255) {
            state->consecutive_low_hashrate++;
        }
        result = state->consecutive_low_hashrate;
        xSemaphoreGive(state->mutex);
    }

    return result;
}

void autotune_state_reset_low_hashrate(autotune_state_t state)
{
    if (!state || !state->mutex) {
        return;
    }

    if (xSemaphoreTake(state->mutex, portMAX_DELAY) == pdTRUE) {
        state->consecutive_low_hashrate = 0;
        xSemaphoreGive(state->mutex);
    }
}

/* ============================================
 * Validation
 * ============================================ */

bool autotune_state_is_valid(autotune_state_t state)
{
    if (!state) {
        return false;
    }

    return state->initialized && (state->mutex != NULL);
}
