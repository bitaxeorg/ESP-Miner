#include "bzm/hashrate.h"
#include <stddef.h>

static void append(bzm_hashrate_t *measurement, float rate)
{
    measurement->history[measurement->next] = rate;
    measurement->next = (measurement->next + 1) % BZM_HASHRATE_WINDOW;
    if (measurement->count < BZM_HASHRATE_WINDOW) ++measurement->count;
    double sum = 0.0;
    for (unsigned i = 0; i < measurement->count; ++i) {
        sum += measurement->history[i];
    }
    measurement->sum = sum;
    measurement->rate = sum / measurement->count;
}

bool bzm_hashrate_sample(bzm_hashrate_t *measurement, uint64_t counter,
                        uint64_t time_us, float *interval_hashrate)
{
    if (measurement == NULL || interval_hashrate == NULL) return false;
    *interval_hashrate = 0;
    if (measurement->baseline_valid && time_us == measurement->time_us) return false;

    bool valid = measurement->baseline_valid && time_us > measurement->time_us &&
                 counter >= measurement->counter;
    if (valid) {
        uint64_t elapsed_us = time_us - measurement->time_us;
        *interval_hashrate = (double)(counter - measurement->counter) * 4294967296.0 / elapsed_us / 1000.0;
        append(measurement, *interval_hashrate);
    } else {
        measurement->rate = 0;
    }
    measurement->counter = counter;
    measurement->time_us = time_us;
    measurement->baseline_valid = true;
    return valid;
}
