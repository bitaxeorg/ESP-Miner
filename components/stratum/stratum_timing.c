#include "stratum_timing.h"
#include <string.h>

void stratum_timing_record(stratum_timing_tracker_t *tracker, uint32_t id, uint64_t submit_time_us)
{
    if (!tracker) {
        return;
    }
    tracker->submit_time_us[id % STRATUM_TIMING_SLOTS] = submit_time_us;
}

float stratum_timing_calculate_ms(stratum_timing_tracker_t *tracker, uint32_t id, uint64_t now_us)
{
    if (!tracker) {
        return -1.0f;
    }
    uint32_t slot = id % STRATUM_TIMING_SLOTS;
    uint64_t submit_time_us = tracker->submit_time_us[slot];
    if (submit_time_us == 0 || now_us < submit_time_us) {
        return -1.0f;
    }
    tracker->submit_time_us[slot] = 0;
    return (float)(now_us - submit_time_us) / 1000.0f;
}

void stratum_timing_reset(stratum_timing_tracker_t *tracker)
{
    if (!tracker) {
        return;
    }
    memset(tracker->submit_time_us, 0, sizeof(tracker->submit_time_us));
}
