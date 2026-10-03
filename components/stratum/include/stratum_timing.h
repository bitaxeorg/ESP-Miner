#ifndef STRATUM_TIMING_H
#define STRATUM_TIMING_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STRATUM_TIMING_SLOTS 64

typedef struct {
    uint64_t submit_time_us[STRATUM_TIMING_SLOTS];
} stratum_timing_tracker_t;

/**
 * @brief Record a submission timestamp for a given request / share ID.
 *
 * @param tracker Pointer to the timing tracker.
 * @param id The request ID / share ID / sequence number.
 * @param submit_time_us The submission timestamp in microseconds.
 */
void stratum_timing_record(stratum_timing_tracker_t *tracker, uint32_t id, uint64_t submit_time_us);

/**
 * @brief Calculate round-trip response time in milliseconds for an ID, and clear the slot.
 *
 * @param tracker Pointer to the timing tracker.
 * @param id The response ID / share ID / sequence number.
 * @param now_us The current timestamp in microseconds.
 * @return Latency in milliseconds, or -1.0f if the ID was not tracked or invalid.
 */
float stratum_timing_calculate_ms(stratum_timing_tracker_t *tracker, uint32_t id, uint64_t now_us);

/**
 * @brief Reset/clear all tracking slots (e.g. on disconnect or pool switch).
 *
 * @param tracker Pointer to the timing tracker.
 */
void stratum_timing_reset(stratum_timing_tracker_t *tracker);

#ifdef __cplusplus
}
#endif

#endif /* STRATUM_TIMING_H */
