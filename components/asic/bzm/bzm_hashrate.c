#include "bzm/hashrate.h"

static void append(bzm_hashrate_t *measurement, float rate)
{
    measurement->sum += (double)rate - measurement->history[measurement->next];
    measurement->history[measurement->next] = rate;
    measurement->next = (measurement->next + 1) % BZM_HASHRATE_WINDOW;
    if (measurement->count < BZM_HASHRATE_WINDOW) ++measurement->count;
    measurement->rate = measurement->sum / measurement->count;
}

void bzm_hashrate_sample(bzm_hashrate_t *measurement, uint64_t counter,
                         uint64_t time_us, bool active)
{
    if (!measurement->sampled) {
        measurement->sampled = true;
        measurement->counter = counter;
        measurement->time_us = time_us;
        measurement->active = active;
        append(measurement, 0);
        return;
    }
    if (!active || time_us < measurement->time_us) {
        measurement->counter = counter;
        measurement->active = false;
    }
    if (time_us < measurement->time_us) measurement->time_us = time_us;
    uint64_t elapsed_us = time_us - measurement->time_us;
    if (elapsed_us < 1000000) return;

    float rate = active && measurement->active
        ? (double)(counter - measurement->counter) * 4294967296.0 / elapsed_us / 1000.0 : 0;
    uint64_t intervals = elapsed_us / 1000000;
    if (intervals > BZM_HASHRATE_WINDOW) intervals = BZM_HASHRATE_WINDOW;
    for (uint64_t i = 0; i < intervals; ++i) append(measurement, rate);
    measurement->counter = counter;
    measurement->time_us = time_us;
    measurement->active = active;
}
