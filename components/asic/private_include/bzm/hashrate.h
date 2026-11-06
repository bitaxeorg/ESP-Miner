#ifndef BZM_HASHRATE_H
#define BZM_HASHRATE_H

#include <stdbool.h>
#include <stdint.h>

#define BZM_HASHRATE_WINDOW 60U

typedef struct {
    float history[BZM_HASHRATE_WINDOW];
    double sum;
    uint64_t counter;
    uint64_t time_us;
    float rate;
    unsigned count;
    unsigned next;
    bool sampled;
    bool active;
} bzm_hashrate_t;

void bzm_hashrate_sample(bzm_hashrate_t *measurement, uint64_t counter,
                         uint64_t time_us, bool active);

#endif
